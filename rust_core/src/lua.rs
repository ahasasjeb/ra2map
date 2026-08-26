//! Sandboxed Lua 5.5 host for map scripts.
//!
//! The Lua VM never receives direct filesystem or process access. All map
//! access crosses an explicit callback table supplied by the editor, which
//! runs the script against a transactional INI copy and commits on success.

use mlua::chunk::ChunkMode;
use mlua::{Error, HookTriggers, Lua, LuaOptions, StdLib, Table, Value, Variadic, VmState};
use std::cell::Cell;
use std::ffi::{c_char, c_void, CStr, CString};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;
use std::rc::Rc;

const RS_OK: i32 = 0;
const RS_ERR_BAD_ARG: i32 = -1;
const RS_ERR_PANIC: i32 = -3;
const RS_ERR_LUA_RUNTIME: i32 = -20;

const MEMORY_LIMIT: usize = 64 * 1024 * 1024;
const INVOKE_RESULT_LIMIT: usize = 4 * 1024 * 1024;
const HOOK_INTERVAL: u32 = 10_000;
const INSTRUCTION_LIMIT: u64 = 20_000_000;

const MUTATE_SET: i32 = 0;
const MUTATE_REMOVE_KEY: i32 = 1;
const MUTATE_CLEAR_SECTION: i32 = 2;
const MUTATE_REMOVE_SECTION: i32 = 3;

pub type RsLuaGetCallback = unsafe extern "C" fn(
    context: *mut c_void,
    section: *const c_char,
    key: *const c_char,
    dst: *mut c_char,
    dst_cap: usize,
    out_len: *mut usize,
) -> i32;

pub type RsLuaListCallback = unsafe extern "C" fn(
    context: *mut c_void,
    section: *const c_char,
    dst: *mut c_char,
    dst_cap: usize,
) -> usize;

pub type RsLuaMutateCallback = unsafe extern "C" fn(
    context: *mut c_void,
    operation: i32,
    section: *const c_char,
    key: *const c_char,
    value: *const c_char,
) -> i32;

pub type RsLuaPrintCallback = unsafe extern "C" fn(context: *mut c_void, text: *const c_char);

pub type RsLuaInvokeCallback = unsafe extern "C" fn(
    context: *mut c_void,
    operation: *const c_char,
    args: *const *const c_char,
    arg_count: usize,
    dst: *mut c_char,
    dst_cap: usize,
    out_len: *mut usize,
) -> i32;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RsLuaCallbacks {
    pub get: Option<RsLuaGetCallback>,
    pub list: Option<RsLuaListCallback>,
    pub mutate: Option<RsLuaMutateCallback>,
    pub print: Option<RsLuaPrintCallback>,
    pub invoke: Option<RsLuaInvokeCallback>,
}

fn runtime_error(message: impl Into<String>) -> Error {
    Error::RuntimeError(message.into())
}

fn c_string(text: &str, label: &str) -> mlua::Result<CString> {
    CString::new(text).map_err(|_| runtime_error(format!("{label} contains a NUL byte")))
}

unsafe fn host_get(
    callbacks: RsLuaCallbacks,
    context: *mut c_void,
    section: &str,
    key: &str,
) -> mlua::Result<Option<String>> {
    let get = callbacks
        .get
        .ok_or_else(|| runtime_error("map.get is unavailable"))?;
    let section = c_string(section, "section name")?;
    let key = c_string(key, "key name")?;
    let mut len = 0usize;
    let found = unsafe {
        get(
            context,
            section.as_ptr(),
            key.as_ptr(),
            ptr::null_mut(),
            0,
            &mut len,
        )
    };
    if found < 0 {
        return Err(runtime_error("the editor rejected a map read"));
    }
    if found == 0 {
        return Ok(None);
    }

    let mut bytes = vec![0u8; len];
    let read_result = unsafe {
        get(
            context,
            section.as_ptr(),
            key.as_ptr(),
            bytes.as_mut_ptr().cast(),
            bytes.len(),
            &mut len,
        )
    };
    if read_result != 1 || len > bytes.len() {
        return Err(runtime_error("the editor returned an invalid map value"));
    }
    bytes.truncate(len);
    String::from_utf8(bytes)
        .map(Some)
        .map_err(|_| runtime_error("the map value is not valid UTF-8"))
}

unsafe fn host_list(
    callbacks: RsLuaCallbacks,
    context: *mut c_void,
    section: Option<&str>,
) -> mlua::Result<Vec<String>> {
    let list = callbacks
        .list
        .ok_or_else(|| runtime_error("map enumeration is unavailable"))?;
    let section = section
        .map(|name| c_string(name, "section name"))
        .transpose()?;
    let section_ptr = section.as_ref().map_or(ptr::null(), |name| name.as_ptr());
    let needed = unsafe { list(context, section_ptr, ptr::null_mut(), 0) };
    if needed == 0 {
        return Ok(Vec::new());
    }

    let mut bytes = vec![0u8; needed];
    let written = unsafe { list(context, section_ptr, bytes.as_mut_ptr().cast(), bytes.len()) };
    if written != needed {
        return Err(runtime_error("the editor returned an invalid map list"));
    }

    let mut result = Vec::new();
    for item in bytes
        .split(|byte| *byte == 0)
        .filter(|item| !item.is_empty())
    {
        result.push(
            String::from_utf8(item.to_vec())
                .map_err(|_| runtime_error("a map name is not valid UTF-8"))?,
        );
    }
    Ok(result)
}

unsafe fn host_mutate(
    callbacks: RsLuaCallbacks,
    context: *mut c_void,
    operation: i32,
    section: &str,
    key: Option<&str>,
    value: Option<&str>,
) -> mlua::Result<()> {
    let mutate = callbacks
        .mutate
        .ok_or_else(|| runtime_error("map mutation is unavailable"))?;
    let section = c_string(section, "section name")?;
    let key = key.map(|text| c_string(text, "key name")).transpose()?;
    let value = value.map(|text| c_string(text, "value")).transpose()?;
    let result = unsafe {
        mutate(
            context,
            operation,
            section.as_ptr(),
            key.as_ref().map_or(ptr::null(), |text| text.as_ptr()),
            value.as_ref().map_or(ptr::null(), |text| text.as_ptr()),
        )
    };
    if result == 1 {
        Ok(())
    } else {
        Err(runtime_error("the editor rejected a map mutation"))
    }
}

unsafe fn host_print(
    callbacks: RsLuaCallbacks,
    context: *mut c_void,
    text: &str,
) -> mlua::Result<()> {
    let Some(print) = callbacks.print else {
        return Ok(());
    };
    let text = c_string(text, "printed text")?;
    unsafe { print(context, text.as_ptr()) };
    Ok(())
}

unsafe fn host_invoke(
    callbacks: RsLuaCallbacks,
    context: *mut c_void,
    operation: &str,
    args: &[String],
) -> mlua::Result<Vec<u8>> {
    let invoke = callbacks
        .invoke
        .ok_or_else(|| runtime_error("editor operations are unavailable"))?;
    let operation_name = operation.to_owned();
    let response_limit = if operation == "module.load"
        || operation.starts_with("game.")
        || operation.ends_with(".list")
    {
        INVOKE_RESULT_LIMIT
    } else {
        64 * 1024
    };
    let operation = c_string(operation, "operation name")?;
    let args = args
        .iter()
        .map(|value| c_string(value, "operation argument"))
        .collect::<mlua::Result<Vec<_>>>()?;
    let arg_ptrs = args.iter().map(|value| value.as_ptr()).collect::<Vec<_>>();
    let mut response = vec![0u8; response_limit];
    let mut response_len = 0usize;
    let result = unsafe {
        invoke(
            context,
            operation.as_ptr(),
            arg_ptrs.as_ptr(),
            arg_ptrs.len(),
            response.as_mut_ptr().cast(),
            response.len(),
            &mut response_len,
        )
    };
    if response_len > response.len() {
        return Err(runtime_error(
            "the editor returned an oversized operation result",
        ));
    }
    response.truncate(response_len);
    if result != 1 {
        let detail = String::from_utf8_lossy(&response);
        let message = if detail.is_empty() {
            format!("the editor rejected operation '{operation_name}'")
        } else {
            detail.into_owned()
        };
        return Err(runtime_error(message));
    }
    Ok(response)
}

fn value_to_text(value: Value) -> mlua::Result<String> {
    match value {
        Value::String(text) => Ok(text.to_string_lossy()),
        Value::Integer(number) => Ok(number.to_string()),
        Value::Number(number) => Ok(number.to_string()),
        Value::Boolean(value) => Ok(if value { "true" } else { "false" }.to_owned()),
        Value::Nil => Ok("nil".to_owned()),
        other => Err(runtime_error(format!(
            "cannot convert {} to an INI value",
            other.type_name()
        ))),
    }
}

fn install_map_api(lua: &Lua, callbacks: RsLuaCallbacks, context: *mut c_void) -> mlua::Result<()> {
    let map = lua.create_table()?;

    let get_callbacks = callbacks;
    map.set(
        "get",
        lua.create_function(
            move |_, (section, key, default): (String, String, Option<String>)| unsafe {
                Ok(host_get(get_callbacks, context, &section, &key)?.or(default))
            },
        )?,
    )?;

    let has_callbacks = callbacks;
    map.set(
        "has",
        lua.create_function(move |_, (section, key): (String, String)| unsafe {
            Ok(host_get(has_callbacks, context, &section, &key)?.is_some())
        })?,
    )?;

    let set_callbacks = callbacks;
    map.set(
        "set",
        lua.create_function(
            move |_, (section, key, value): (String, String, Value)| unsafe {
                let value = value_to_text(value)?;
                host_mutate(
                    set_callbacks,
                    context,
                    MUTATE_SET,
                    &section,
                    Some(&key),
                    Some(&value),
                )
            },
        )?,
    )?;

    let remove_callbacks = callbacks;
    map.set(
        "remove",
        lua.create_function(move |_, (section, key): (String, String)| unsafe {
            let existed = host_get(remove_callbacks, context, &section, &key)?.is_some();
            host_mutate(
                remove_callbacks,
                context,
                MUTATE_REMOVE_KEY,
                &section,
                Some(&key),
                None,
            )?;
            Ok(existed)
        })?,
    )?;

    let clear_callbacks = callbacks;
    map.set(
        "clear_section",
        lua.create_function(move |_, section: String| unsafe {
            host_mutate(
                clear_callbacks,
                context,
                MUTATE_CLEAR_SECTION,
                &section,
                None,
                None,
            )
        })?,
    )?;

    let delete_callbacks = callbacks;
    map.set(
        "remove_section",
        lua.create_function(move |_, section: String| unsafe {
            host_mutate(
                delete_callbacks,
                context,
                MUTATE_REMOVE_SECTION,
                &section,
                None,
                None,
            )
        })?,
    )?;

    let sections_callbacks = callbacks;
    map.set(
        "sections",
        lua.create_function(move |lua, ()| unsafe {
            lua.create_sequence_from(host_list(sections_callbacks, context, None)?)
        })?,
    )?;

    let keys_callbacks = callbacks;
    map.set(
        "keys",
        lua.create_function(move |lua, section: String| unsafe {
            lua.create_sequence_from(host_list(keys_callbacks, context, Some(&section))?)
        })?,
    )?;

    let section_callbacks = callbacks;
    map.set(
        "section",
        lua.create_function(move |lua, section: String| unsafe {
            let result = lua.create_table()?;
            for key in host_list(section_callbacks, context, Some(&section))? {
                if let Some(value) = host_get(section_callbacks, context, &section, &key)? {
                    result.set(key, value)?;
                }
            }
            Ok(result)
        })?,
    )?;

    let replace_callbacks = callbacks;
    map.set(
        "replace_section",
        lua.create_function(move |_, (section, values): (String, Table)| unsafe {
            let mut entries = Vec::new();
            for pair in values.pairs::<String, Value>() {
                let (key, value) = pair?;
                entries.push((key, value_to_text(value)?));
            }
            host_mutate(
                replace_callbacks,
                context,
                MUTATE_CLEAR_SECTION,
                &section,
                None,
                None,
            )?;
            for (key, value) in entries {
                host_mutate(
                    replace_callbacks,
                    context,
                    MUTATE_SET,
                    &section,
                    Some(&key),
                    Some(&value),
                )?;
            }
            Ok(())
        })?,
    )?;

    let info = lua.create_table()?;
    for (field, key) in [
        ("width", "width"),
        ("height", "height"),
        ("iso_size", "iso_size"),
        ("waypoint_count", "waypoint_count"),
        ("unit_count", "unit_count"),
        ("infantry_count", "infantry_count"),
        ("structure_count", "structure_count"),
        ("aircraft_count", "aircraft_count"),
        ("terrain_count", "terrain_count"),
        ("player_count", "player_count"),
        ("house_count", "house_count"),
        ("country_count", "country_count"),
    ] {
        if let Some(value) = unsafe { host_get(callbacks, context, "$editor", key)? } {
            if let Ok(number) = value.parse::<i64>() {
                info.set(field, number)?;
            }
        }
    }
    if let Some(value) = unsafe { host_get(callbacks, context, "$editor", "theater")? } {
        info.set("theater", value)?;
    }
    if let Some(value) = unsafe { host_get(callbacks, context, "$editor", "multiplayer")? } {
        info.set("multiplayer", value == "1")?;
    }
    map.set("info", info)?;
    map.set("api_version", 2)?;

    lua.globals().set("map", map)?;
    Ok(())
}

fn install_editor_api(
    lua: &Lua,
    callbacks: RsLuaCallbacks,
    context: *mut c_void,
) -> mlua::Result<()> {
    let editor = lua.create_table()?;
    editor.set(
        "invoke",
        lua.create_function(move |lua, values: Variadic<Value>| unsafe {
            let Some(operation) = values.first() else {
                return Err(runtime_error("editor.invoke requires an operation name"));
            };
            let operation = match operation {
                Value::String(value) => value.to_string_lossy(),
                _ => return Err(runtime_error("editor.invoke operation must be a string")),
            };
            let mut args = Vec::with_capacity(values.len().saturating_sub(1));
            for value in values.into_iter().skip(1) {
                args.push(value_to_text(value)?);
            }
            lua.create_string(host_invoke(callbacks, context, &operation, &args)?)
        })?,
    )?;
    lua.globals().set("editor", editor)
}

fn install_safe_require(
    lua: &Lua,
    callbacks: RsLuaCallbacks,
    context: *mut c_void,
) -> mlua::Result<()> {
    let loaded = lua.create_table()?;
    lua.globals().set(
        "require",
        lua.create_function(move |lua, name: String| {
            let existing: Value = loaded.get(name.clone())?;
            if !matches!(existing, Value::Nil) {
                return Ok(existing);
            }

            loaded.set(name.clone(), true)?;
            let source = unsafe {
                host_invoke(
                    callbacks,
                    context,
                    "module.load",
                    std::slice::from_ref(&name),
                )?
            };
            let chunk_name = format!("@Scripts/lib/{}.lua", name.replace('.', "/"));
            match lua.load(&source).set_name(&chunk_name).eval::<Value>() {
                Ok(Value::Nil) => {
                    loaded.set(name, true)?;
                    Ok(Value::Boolean(true))
                }
                Ok(value) => {
                    loaded.set(name, value.clone())?;
                    Ok(value)
                }
                Err(error) => {
                    loaded.set(name, Value::Nil)?;
                    Err(error)
                }
            }
        })?,
    )
}

fn install_print(lua: &Lua, callbacks: RsLuaCallbacks, context: *mut c_void) -> mlua::Result<()> {
    lua.globals().set(
        "print",
        lua.create_function(move |_, values: Variadic<Value>| unsafe {
            let mut fields = Vec::with_capacity(values.len());
            for value in values {
                fields.push(value_to_text(value)?);
            }
            host_print(callbacks, context, &fields.join("\t"))
        })?,
    )
}

fn run_lua(
    source: &[u8],
    source_name: &str,
    callbacks: RsLuaCallbacks,
    context: *mut c_void,
) -> mlua::Result<()> {
    let libraries = StdLib::TABLE | StdLib::STRING | StdLib::MATH | StdLib::UTF8;
    let lua = Lua::new_with(libraries, LuaOptions::default())?;
    lua.set_memory_limit(MEMORY_LIMIT)?;

    // Defense in depth: these are absent without IO/OS/PACKAGE, but explicitly
    // clear the common loader entry points in case mlua defaults change.
    let globals = lua.globals();
    for name in ["dofile", "loadfile", "require", "load"] {
        globals.set(name, Value::Nil)?;
    }

    install_map_api(&lua, callbacks, context)?;
    install_print(&lua, callbacks, context)?;
    install_editor_api(&lua, callbacks, context)?;
    install_safe_require(&lua, callbacks, context)?;
    lua.load(include_str!("lua_api.lua"))
        .set_name("@map_api")
        .exec()?;

    let instruction_count = Rc::new(Cell::new(0u64));
    let hook_count = Rc::clone(&instruction_count);
    lua.set_hook(
        HookTriggers::new().every_nth_instruction(HOOK_INTERVAL),
        move |_, _| {
            let count = hook_count.get().saturating_add(HOOK_INTERVAL as u64);
            hook_count.set(count);
            if count > INSTRUCTION_LIMIT {
                Err(runtime_error(format!(
                    "script exceeded the {INSTRUCTION_LIMIT} instruction limit"
                )))
            } else {
                Ok(VmState::Continue)
            }
        },
    )?;

    lua.load(source)
        .set_name(source_name)
        .set_mode(ChunkMode::Text)
        .exec()
}

unsafe fn write_error(dst: *mut c_char, dst_cap: usize, message: &str) {
    if dst.is_null() || dst_cap == 0 {
        return;
    }
    let bytes = message.as_bytes();
    let copy_len = bytes.len().min(dst_cap - 1);
    unsafe {
        ptr::copy_nonoverlapping(bytes.as_ptr(), dst.cast(), copy_len);
        *dst.add(copy_len) = 0;
    }
}

/// Executes a sandboxed Lua map script. Host changes are owned by the callback
/// context; the C++ caller commits its transactional copy only when RS_OK is
/// returned.
#[no_mangle]
pub unsafe extern "C" fn rs_lua_run(
    source: *const u8,
    source_len: usize,
    source_name: *const c_char,
    callbacks: *const RsLuaCallbacks,
    context: *mut c_void,
    error: *mut c_char,
    error_cap: usize,
) -> i32 {
    if source.is_null()
        || callbacks.is_null()
        || context.is_null()
        || unsafe { (*callbacks).get.is_none() }
        || unsafe { (*callbacks).list.is_none() }
        || unsafe { (*callbacks).mutate.is_none() }
        || unsafe { (*callbacks).invoke.is_none() }
    {
        unsafe { write_error(error, error_cap, "invalid Lua host arguments") };
        return RS_ERR_BAD_ARG;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let source = unsafe { std::slice::from_raw_parts(source, source_len) };
        let source_name = if source_name.is_null() {
            "map_script.lua".to_owned()
        } else {
            unsafe { CStr::from_ptr(source_name) }
                .to_string_lossy()
                .into_owned()
        };
        run_lua(source, &source_name, unsafe { *callbacks }, context)
    }));

    match result {
        Ok(Ok(())) => {
            unsafe { write_error(error, error_cap, "") };
            RS_OK
        }
        Ok(Err(lua_error)) => {
            unsafe { write_error(error, error_cap, &lua_error.to_string()) };
            RS_ERR_LUA_RUNTIME
        }
        Err(_) => {
            unsafe { write_error(error, error_cap, "panic in Lua runtime") };
            RS_ERR_PANIC
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::BTreeMap;

    #[derive(Default)]
    struct Host {
        sections: BTreeMap<String, BTreeMap<String, String>>,
        output: Vec<String>,
    }

    unsafe fn text(pointer: *const c_char) -> String {
        unsafe { CStr::from_ptr(pointer) }
            .to_string_lossy()
            .into_owned()
    }

    unsafe extern "C" fn get(
        context: *mut c_void,
        section: *const c_char,
        key: *const c_char,
        dst: *mut c_char,
        dst_cap: usize,
        out_len: *mut usize,
    ) -> i32 {
        let host = unsafe { &mut *(context as *mut Host) };
        let Some(value) = host
            .sections
            .get(&unsafe { text(section) })
            .and_then(|section| section.get(&unsafe { text(key) }))
        else {
            return 0;
        };
        unsafe { *out_len = value.len() };
        if !dst.is_null() && dst_cap >= value.len() {
            unsafe { ptr::copy_nonoverlapping(value.as_ptr(), dst.cast(), value.len()) };
        }
        1
    }

    unsafe extern "C" fn list(
        context: *mut c_void,
        section: *const c_char,
        dst: *mut c_char,
        dst_cap: usize,
    ) -> usize {
        let host = unsafe { &mut *(context as *mut Host) };
        let names: Vec<&str> = if section.is_null() {
            host.sections.keys().map(String::as_str).collect()
        } else {
            host.sections
                .get(&unsafe { text(section) })
                .map(|values| values.keys().map(String::as_str).collect())
                .unwrap_or_default()
        };
        let needed = names.iter().map(|name| name.len() + 1).sum();
        if !dst.is_null() && dst_cap >= needed {
            let mut offset = 0;
            for name in names {
                unsafe {
                    ptr::copy_nonoverlapping(name.as_ptr(), dst.add(offset).cast(), name.len())
                };
                offset += name.len();
                unsafe { *dst.add(offset) = 0 };
                offset += 1;
            }
        }
        needed
    }

    unsafe extern "C" fn mutate(
        context: *mut c_void,
        operation: i32,
        section: *const c_char,
        key: *const c_char,
        value: *const c_char,
    ) -> i32 {
        let host = unsafe { &mut *(context as *mut Host) };
        let section = unsafe { text(section) };
        match operation {
            MUTATE_SET => {
                host.sections
                    .entry(section)
                    .or_default()
                    .insert(unsafe { text(key) }, unsafe { text(value) });
            }
            MUTATE_REMOVE_KEY => {
                if let Some(values) = host.sections.get_mut(&section) {
                    values.remove(&unsafe { text(key) });
                }
            }
            MUTATE_CLEAR_SECTION => {
                host.sections.entry(section).or_default().clear();
            }
            MUTATE_REMOVE_SECTION => {
                host.sections.remove(&section);
            }
            _ => return 0,
        }
        1
    }

    unsafe extern "C" fn print(context: *mut c_void, value: *const c_char) {
        let host = unsafe { &mut *(context as *mut Host) };
        host.output.push(unsafe { text(value) });
    }

    unsafe extern "C" fn invoke(
        context: *mut c_void,
        operation: *const c_char,
        _args: *const *const c_char,
        _arg_count: usize,
        dst: *mut c_char,
        dst_cap: usize,
        out_len: *mut usize,
    ) -> i32 {
        let operation = unsafe { CStr::from_ptr(operation) }.to_string_lossy();
        let host = unsafe { &mut *(context as *mut Host) };
        let response: Vec<u8> = match operation.as_ref() {
            "capabilities" => b"test\0".to_vec(),
            "id.free_global" => {
                if host
                    .sections
                    .get("Triggers")
                    .is_some_and(|values| !values.is_empty())
                {
                    b"01000001".to_vec()
                } else {
                    b"01000000".to_vec()
                }
            }
            "id.free_numeric" => b"0".to_vec(),
            "module.load" => b"return { answer = 42 }".to_vec(),
            _ => Vec::new(),
        };
        unsafe { *out_len = response.len() };
        if response.len() > dst_cap {
            return 0;
        }
        unsafe { ptr::copy_nonoverlapping(response.as_ptr(), dst.cast(), response.len()) };
        1
    }

    fn callbacks() -> RsLuaCallbacks {
        RsLuaCallbacks {
            get: Some(get),
            list: Some(list),
            mutate: Some(mutate),
            print: Some(print),
            invoke: Some(invoke),
        }
    }

    #[test]
    fn executes_map_api_without_unsafe_libraries() {
        let mut host = Host::default();
        host.sections
            .entry("Basic".to_owned())
            .or_default()
            .insert("Name".to_owned(), "Before".to_owned());
        host.sections
            .entry("$editor".to_owned())
            .or_default()
            .insert("width".to_owned(), "80".to_owned());
        let script = br#"
            assert(_VERSION == "Lua 5.5")
            assert(os == nil and io == nil and package == nil and debug == nil)
            assert(type(require) == "function" and dofile == nil and loadfile == nil and load == nil)
            assert(map.get("Basic", "Name") == "Before")
            assert(map.has("Basic", "Name"))
            map.set("Basic", "Name", "After")
            map.set("Basic", "Number", 42)
            assert(#map.keys("Basic") == 2)
            assert(map.info.width == 80)
            print("updated", map.get("Basic", "Name"))
        "#;
        let name = CString::new("test.lua").unwrap();
        let mut error = [0i8; 512];
        let result = unsafe {
            rs_lua_run(
                script.as_ptr(),
                script.len(),
                name.as_ptr(),
                &callbacks(),
                (&mut host as *mut Host).cast(),
                error.as_mut_ptr(),
                error.len(),
            )
        };
        assert_eq!(result, RS_OK, "{}", unsafe {
            CStr::from_ptr(error.as_ptr()).to_string_lossy()
        });
        assert_eq!(host.sections["Basic"]["Name"], "After");
        assert_eq!(host.sections["Basic"]["Number"], "42");
        assert_eq!(host.output, ["updated\tAfter"]);
    }

    #[test]
    fn high_level_api_creates_objects_and_trigger_graphs() {
        let mut host = Host::default();
        let script = br#"
            assert(map.api_version == 2)
            local unit_id = map.objects.units.create {
                house = "GDI", type = "MTNK", x = 12, y = 34
            }
            assert(unit_id == "0")
            assert(map.objects.units.get(unit_id).type == "MTNK")
            map.objects.units.move(unit_id, 20, 21)
            assert(map.objects.units.get(unit_id).x == "20")

            local trigger = map.triggers.create {
                name = "Lua attack",
                events = { { 13, 0, 10 } },
                actions = { { 4, 0, 0, 0, 0, 0, 0, "A" } },
                create_tag = true,
            }
            assert(trigger.id == "01000000")
            assert(map.get("Events", trigger.id) == "1,13,0,10")
            assert(#map.triggers.get(trigger.id).tags == 1)
            map.triggers.update(trigger.id, { name = "Updated trigger" })
            assert(map.triggers.get(trigger.id).name == "Updated trigger")
            assert(require("sample").answer == 42)
            assert(require("sample").answer == 42)
        "#;
        let name = CString::new("high_level.lua").unwrap();
        let mut error = [0i8; 512];
        let result = unsafe {
            rs_lua_run(
                script.as_ptr(),
                script.len(),
                name.as_ptr(),
                &callbacks(),
                (&mut host as *mut Host).cast(),
                error.as_mut_ptr(),
                error.len(),
            )
        };
        assert_eq!(result, RS_OK, "{}", unsafe {
            CStr::from_ptr(error.as_ptr()).to_string_lossy()
        });
        assert_eq!(host.sections["Units"]["0"].split(',').nth(1), Some("MTNK"));
        assert_eq!(host.sections["Triggers"].len(), 1);
        assert_eq!(host.sections["Tags"].len(), 1);
    }

    #[test]
    fn reports_syntax_errors() {
        let mut host = Host::default();
        let script = b"this is not lua";
        let mut error = [0i8; 512];
        let result = unsafe {
            rs_lua_run(
                script.as_ptr(),
                script.len(),
                ptr::null(),
                &callbacks(),
                (&mut host as *mut Host).cast(),
                error.as_mut_ptr(),
                error.len(),
            )
        };
        assert_eq!(result, RS_ERR_LUA_RUNTIME);
        assert!(!unsafe { CStr::from_ptr(error.as_ptr()) }
            .to_bytes()
            .is_empty());
    }

    #[test]
    fn stops_runaway_scripts_at_instruction_limit() {
        let mut host = Host::default();
        let script = b"while true do end";
        let mut error = [0i8; 512];
        let result = unsafe {
            rs_lua_run(
                script.as_ptr(),
                script.len(),
                ptr::null(),
                &callbacks(),
                (&mut host as *mut Host).cast(),
                error.as_mut_ptr(),
                error.len(),
            )
        };
        assert_eq!(result, RS_ERR_LUA_RUNTIME);
        assert!(unsafe { CStr::from_ptr(error.as_ptr()) }
            .to_string_lossy()
            .contains("instruction limit"));
    }
}
