-- High-level map scripting library. This file is loaded by the Rust host
-- after the sandbox and the primitive map/editor functions are installed.

local raw_map = {
    get = map.get,
    has = map.has,
    set = map.set,
    remove = map.remove,
    sections = map.sections,
    keys = map.keys,
    section = map.section,
    replace_section = map.replace_section,
    clear_section = map.clear_section,
    remove_section = map.remove_section,
}
map.ini = raw_map

local function copy_table(value)
    local result = {}
    if value then
        for key, item in pairs(value) do result[key] = item end
    end
    return result
end

local function split_plain(value, separator)
    local result = {}
    if value == nil or value == "" then return result end
    local start = 1
    while true do
        local position = string.find(value, separator, start, true)
        if not position then
            result[#result + 1] = string.sub(value, start)
            break
        end
        result[#result + 1] = string.sub(value, start, position - 1)
        start = position + #separator
    end
    return result
end

local function split_csv(value)
    if value == nil then return {} end
    local result = {}
    local start = 1
    while true do
        local position = string.find(value, ",", start, true)
        if not position then
            result[#result + 1] = string.sub(value, start)
            break
        end
        result[#result + 1] = string.sub(value, start, position - 1)
        start = position + 1
    end
    return result
end

local function join_csv(values)
    local result = {}
    for index, value in ipairs(values or {}) do result[index] = tostring(value) end
    return table.concat(result, ",")
end

local function numeric_flag(value, default)
    if value == nil then return default end
    if type(value) == "boolean" then return value and 1 or 0 end
    return value
end

local function list_result(operation, ...)
    return split_plain(editor.invoke(operation, ...), "\0")
end

local function record_text(value)
    if type(value) == "string" then return value end
    if type(value) ~= "table" then error("record must be a string or table", 3) end
    if value.raw ~= nil then return tostring(value.raw) end
    if value.record ~= nil then return tostring(value.record) end
    return join_csv(value.fields or value)
end

local function decode_record(id, raw, schema)
    local fields = split_csv(raw)
    local result = { id = id, raw = raw, fields = fields }
    if schema then
        for index, name in ipairs(schema) do result[name] = fields[index] end
    end
    return result
end

local function encode_record(value, schema, defaults, require_type)
    if type(value) == "string" then return value end
    if type(value) ~= "table" then error("record must be a string or table", 3) end
    if value.raw ~= nil then return tostring(value.raw) end
    if value.record ~= nil then return tostring(value.record) end

    local fields = copy_table(defaults)
    if value.fields then
        for index, item in ipairs(value.fields) do fields[index] = item end
    end
    for index, name in ipairs(schema or {}) do
        if value[name] ~= nil then fields[index] = value[name] end
    end
    if require_type and (fields[2] == nil or tostring(fields[2]) == "") then
        error("object type is required", 3)
    end
    return join_csv(fields)
end

map.csv = { split = split_csv, join = join_csv }

map.ids = {}
function map.ids.free()
    return editor.invoke("id.free_global")
end
function map.ids.free_numeric(section)
    return editor.invoke("id.free_numeric", section)
end

map.records = {}
function map.records.get(section, id)
    local raw = map.get(section, tostring(id))
    if raw == nil then return nil end
    return decode_record(tostring(id), raw)
end
function map.records.list(section)
    local result = {}
    for _, id in ipairs(map.keys(section)) do
        result[#result + 1] = decode_record(id, map.get(section, id, ""))
    end
    return result
end
function map.records.create(section, value, id)
    id = id or map.ids.free_numeric(section)
    map.set(section, tostring(id), record_text(value))
    return tostring(id)
end
function map.records.update(section, id, value)
    if not map.has(section, tostring(id)) then error("record does not exist", 2) end
    map.set(section, tostring(id), record_text(value))
    return tostring(id)
end
function map.records.delete(section, id)
    return map.remove(section, tostring(id))
end

map.position = {}
function map.position.encode(x, y)
    x, y = math.tointeger(x), math.tointeger(y)
    if not x or not y or x < 0 or y < 0 then error("coordinates must be non-negative integers", 2) end
    return tostring(x) .. string.format("%03d", y)
end
function map.position.decode(value)
    value = tostring(value or "")
    if #value < 4 then return nil, nil end
    return tonumber(string.sub(value, 1, #value - 3)), tonumber(string.sub(value, -3))
end

local function make_entity_collection(section, schema, defaults, require_type)
    local collection = { section = section, schema = schema }
    function collection.get(id)
        local raw = map.get(section, tostring(id))
        if raw == nil then return nil end
        return decode_record(tostring(id), raw, schema)
    end
    function collection.list()
        local result = {}
        for _, id in ipairs(map.keys(section)) do result[#result + 1] = collection.get(id) end
        return result
    end
    function collection.create(value)
        value = value or {}
        local id = value.id or map.ids.free_numeric(section)
        map.set(section, tostring(id), encode_record(value, schema, defaults, require_type ~= false))
        return tostring(id)
    end
    function collection.update(id, changes)
        local current = collection.get(id)
        if not current then error(section .. " record does not exist", 2) end
        for key, value in pairs(changes or {}) do current[key] = value end
        current.raw = nil
        current.record = nil
        map.set(section, tostring(id), encode_record(current, schema, current.fields, require_type ~= false))
        return tostring(id)
    end
    function collection.move(id, x, y) return collection.update(id, { x = x, y = y }) end
    function collection.delete(id) return map.remove(section, tostring(id)) end
    return collection
end

map.objects = {}
map.objects.units = make_entity_collection("Units",
    { "house", "type", "strength", "y", "x", "direction", "action", "tag", "flag1", "flag2", "flag3", "flag4", "flag5", "flag6" },
    { "Neutral", "", 256, 0, 0, 64, "Guard", "None", 0, -1, 0, -1, 1, 0 })
map.objects.vehicles = map.objects.units
map.objects.infantry = make_entity_collection("Infantry",
    { "house", "type", "strength", "y", "x", "subcell", "action", "direction", "tag", "flag1", "flag2", "flag3", "flag4", "flag5" },
    { "Neutral", "", 256, 0, 0, 0, "Guard", 64, "None", 0, -1, 0, 1, 0 })
map.objects.aircraft = make_entity_collection("Aircraft",
    { "house", "type", "strength", "y", "x", "direction", "action", "tag", "flag1", "flag2", "flag3", "flag4" },
    { "Neutral", "", 256, 0, 0, 64, "Guard", "None", 0, 0, 1, 0 })
map.objects.structures = make_entity_collection("Structures",
    { "house", "type", "strength", "y", "x", "direction", "tag", "flag1", "flag2", "energy", "upgrade_count", "spotlight", "upgrade1", "upgrade2", "upgrade3", "flag3", "flag4" },
    { "Neutral", "", 256, 0, 0, 64, "None", 1, 0, 1, 0, 0, "None", "None", "None", 1, 0 })

map.triggers = {}
local trigger_schema = { "house", "attached_trigger", "name", "disabled", "easy", "normal", "hard", "type" }
local function encode_trigger_parts(parts)
    if type(parts) == "string" then return parts end
    if parts and parts.raw then return tostring(parts.raw) end
    local result = { tostring(#(parts or {})) }
    for _, part in ipairs(parts or {}) do result[#result + 1] = record_text(part) end
    return table.concat(result, ",")
end
function map.triggers.create(value)
    value = value or {}
    local id = value.id or map.ids.free()
    local trigger = value.raw or value.record
    if not trigger then
        trigger = join_csv(value.fields or {
            value.house or "Neutral", value.attached_trigger or "<none>", value.name or "Lua trigger",
            numeric_flag(value.disabled, 0), numeric_flag(value.easy, 1),
            numeric_flag(value.normal, 1), numeric_flag(value.hard, 1), value.type or 0,
        })
    end
    map.set("Triggers", id, trigger)
    map.set("Events", id, encode_trigger_parts(value.events))
    map.set("Actions", id, encode_trigger_parts(value.actions))

    local tag_id
    if value.create_tag or value.tag then
        tag_id = type(value.tag) == "table" and value.tag.id or nil
        tag_id = tag_id or map.ids.free()
        local tag = value.tag
        if type(tag) == "string" then
            map.set("Tags", tag_id, tag)
        elseif type(tag) == "table" and (tag.raw or tag.record or tag.fields) then
            map.set("Tags", tag_id, record_text(tag))
        else
            local tag_name = type(tag) == "table" and tag.name or value.name or "Lua tag"
            local repeat_mode = type(tag) == "table" and tag.repeat_mode or 0
            map.set("Tags", tag_id, join_csv({ repeat_mode, tag_name, id }))
        end
    end
    return { id = id, tag_id = tag_id }
end
function map.triggers.get(id)
    id = tostring(id)
    local trigger = map.get("Triggers", id)
    if trigger == nil then return nil end
    local result = decode_record(id, trigger, trigger_schema)
    result.events_raw = map.get("Events", id, "0")
    result.actions_raw = map.get("Actions", id, "0")
    result.tags = {}
    for _, tag_id in ipairs(map.keys("Tags")) do
        local tag = decode_record(tag_id, map.get("Tags", tag_id, ""))
        if tag.fields[3] == id then result.tags[#result.tags + 1] = tag end
    end
    return result
end
function map.triggers.update(id, changes)
    id, changes = tostring(id), changes or {}
    local current = map.triggers.get(id)
    if not current then error("trigger does not exist", 2) end
    if changes.raw or changes.record or changes.fields then
        map.set("Triggers", id, encode_record(changes, trigger_schema, current.fields, false))
    else
        for _, name in ipairs(trigger_schema) do
            if changes[name] ~= nil then current[name] = changes[name] end
        end
        current.raw = nil
        map.set("Triggers", id, encode_record(current, trigger_schema, current.fields, false))
    end
    if changes.events ~= nil then map.set("Events", id, encode_trigger_parts(changes.events)) end
    if changes.actions ~= nil then map.set("Actions", id, encode_trigger_parts(changes.actions)) end
    return id
end
function map.triggers.set_events(id, events)
    map.set("Events", tostring(id), encode_trigger_parts(events))
end
function map.triggers.set_actions(id, actions)
    map.set("Actions", tostring(id), encode_trigger_parts(actions))
end
function map.triggers.list()
    local result = {}
    for _, id in ipairs(map.keys("Triggers")) do result[#result + 1] = map.triggers.get(id) end
    return result
end
function map.triggers.delete(id, options)
    id, options = tostring(id), options or {}
    local removed = map.remove("Triggers", id)
    map.remove("Events", id)
    map.remove("Actions", id)
    if options.delete_tags ~= false then
        for _, tag_id in ipairs(map.keys("Tags")) do
            local fields = split_csv(map.get("Tags", tag_id, ""))
            if fields[3] == id then map.remove("Tags", tag_id) end
        end
    end
    return removed
end
function map.triggers.clone(id, options)
    local source = map.triggers.get(id)
    if not source then error("trigger does not exist", 2) end
    options = options or {}
    source.id = options.id
    source.record = source.raw
    source.raw = nil
    source.events = source.events_raw
    source.actions = source.actions_raw
    source.create_tag = options.create_tag ~= false
    if options.name then
        local fields = split_csv(source.record)
        fields[3] = options.name
        source.record = join_csv(fields)
    end
    return map.triggers.create(source)
end

local function make_simple_global_collection(section)
    local collection = {}
    function collection.get(id)
        local raw = map.get(section, tostring(id))
        if raw == nil then return nil end
        return decode_record(tostring(id), raw)
    end
    function collection.list()
        local result = {}
        for _, id in ipairs(map.keys(section)) do result[#result + 1] = collection.get(id) end
        return result
    end
    function collection.create(value)
        value = value or {}
        local id = value.id or map.ids.free()
        map.set(section, id, record_text(value))
        return id
    end
    function collection.update(id, value)
        map.set(section, tostring(id), record_text(value))
        return tostring(id)
    end
    function collection.delete(id) return map.remove(section, tostring(id)) end
    return collection
end
map.tags = make_simple_global_collection("Tags")
map.ai_triggers = make_simple_global_collection("AITriggerTypes")

map.registries = {}
function map.registries.list(section)
    local result = {}
    for _, index in ipairs(map.keys(section)) do
        local id = map.get(section, index)
        result[#result + 1] = { index = index, id = id, values = id and map.section(id) or {} }
    end
    return result
end
function map.registries.create(section, values, options)
    options = options or {}
    local id = options.id or map.ids.free()
    local index = options.index or map.ids.free_numeric(section)
    map.set(section, index, id)
    map.replace_section(id, values or {})
    return { id = id, index = index }
end
function map.registries.delete(section, id)
    id = tostring(id)
    for _, index in ipairs(map.keys(section)) do
        if map.get(section, index) == id then map.remove(section, index) end
    end
    return map.remove_section(id)
end
local function require_registered_id(section, id)
    id = tostring(id)
    for _, index in ipairs(map.keys(section)) do
        if map.get(section, index) == id then return id end
    end
    error(section .. " entry does not exist", 3)
end
function map.registries.update(section, id, values)
    id = require_registered_id(section, id)
    for key, value in pairs(values or {}) do map.set(id, key, value) end
    return id
end
function map.registries.replace(section, id, values)
    id = require_registered_id(section, id)
    map.replace_section(id, values or {})
    return id
end

local function registry_wrapper(section, defaults)
    return {
        list = function() return map.registries.list(section) end,
        get = function(id) return map.section(tostring(id)) end,
        create = function(values, options)
            local merged = copy_table(defaults)
            for key, value in pairs(values or {}) do merged[key] = value end
            return map.registries.create(section, merged, options)
        end,
        update = function(id, values) return map.registries.update(section, id, values) end,
        replace = function(id, values) return map.registries.replace(section, id, values) end,
        delete = function(id) return map.registries.delete(section, id) end,
    }
end
map.task_forces = registry_wrapper("TaskForces", { Name = "Lua task force", Group = "-1" })
map.script_types = registry_wrapper("ScriptTypes", { Name = "Lua script type" })
map.team_types = registry_wrapper("TeamTypes", { Name = "Lua team type", VeteranLevel = "1", Full = "yes", Autocreate = "yes" })

map.named_lists = {}
function map.named_lists.list(section)
    local result = {}
    for _, index in ipairs(map.keys(section)) do
        local id = map.get(section, index)
        result[#result + 1] = { index = index, id = id, values = id and map.section(id) or {} }
    end
    return result
end
function map.named_lists.create(section, id, values, options)
    options = options or {}
    local index = options.index or map.ids.free_numeric(section)
    map.set(section, index, tostring(id))
    map.replace_section(tostring(id), values or {})
    return { id = tostring(id), index = index }
end
function map.named_lists.delete(section, id)
    id = tostring(id)
    for _, index in ipairs(map.keys(section)) do
        if map.get(section, index) == id then map.remove(section, index) end
    end
    return map.remove_section(id)
end
function map.named_lists.update(section, id, values)
    id = require_registered_id(section, id)
    for key, value in pairs(values or {}) do map.set(id, key, value) end
    return id
end
function map.named_lists.replace(section, id, values)
    id = require_registered_id(section, id)
    map.replace_section(id, values or {})
    return id
end
local function named_list_wrapper(section)
    return {
        list = function() return map.named_lists.list(section) end,
        get = function(id) return map.section(tostring(id)) end,
        create = function(id, values, options) return map.named_lists.create(section, id, values, options) end,
        update = function(id, values) return map.named_lists.update(section, id, values) end,
        replace = function(id, values) return map.named_lists.replace(section, id, values) end,
        delete = function(id) return map.named_lists.delete(section, id) end,
    }
end
map.houses = named_list_wrapper("Houses")
map.countries = named_list_wrapper("Countries")

map.ai_trigger_enabling = {}
function map.ai_trigger_enabling.list() return map.section("AITriggerTypesEnable") end
function map.ai_trigger_enabling.get(id) return map.get("AITriggerTypesEnable", tostring(id)) end
function map.ai_trigger_enabling.set(id, enabled)
    map.set("AITriggerTypesEnable", tostring(id), enabled and "yes" or "no")
end
function map.ai_trigger_enabling.delete(id) return map.remove("AITriggerTypesEnable", tostring(id)) end

local function section_wrapper(section)
    return {
        get = function(key, default) return map.get(section, key, default) end,
        has = function(key) return map.has(section, key) end,
        set = function(key, value) map.set(section, key, value) end,
        remove = function(key) return map.remove(section, key) end,
        values = function() return map.section(section) end,
        patch = function(values) for key, value in pairs(values or {}) do map.set(section, key, value) end end,
        replace = function(values) map.replace_section(section, values or {}) end,
    }
end
map.basic = section_wrapper("Basic")
map.map_settings = section_wrapper("Map")
map.lighting = section_wrapper("Lighting")
map.special_flags = section_wrapper("SpecialFlags")

map.waypoints = {}
function map.waypoints.list()
    local result = {}
    for _, id in ipairs(map.keys("Waypoints")) do
        local value = map.get("Waypoints", id)
        local x, y = map.position.decode(value)
        result[#result + 1] = { id = id, x = x, y = y, raw = value }
    end
    return result
end
function map.waypoints.get(id)
    local value = map.get("Waypoints", tostring(id))
    if value == nil then return nil end
    local x, y = map.position.decode(value)
    return { id = tostring(id), x = x, y = y, raw = value }
end
function map.waypoints.set(id, x, y)
    id = id == nil and map.ids.free_numeric("Waypoints") or tostring(id)
    map.set("Waypoints", id, map.position.encode(x, y))
    return id
end
function map.waypoints.delete(id) return map.remove("Waypoints", tostring(id)) end

map.cell_tags = {}
function map.cell_tags.set(x, y, tag_id)
    local position = map.position.encode(x, y)
    map.set("CellTags", position, tostring(tag_id))
    return position
end
function map.cell_tags.get(x, y) return map.get("CellTags", map.position.encode(x, y)) end
function map.cell_tags.delete(x, y) return map.remove("CellTags", map.position.encode(x, y)) end
function map.cell_tags.list()
    local result = {}
    for _, position in ipairs(map.keys("CellTags")) do
        local x, y = map.position.decode(position)
        result[#result + 1] = { position = position, x = x, y = y, tag_id = map.get("CellTags", position) }
    end
    return result
end

map.local_variables = make_entity_collection("VariableNames", { "name", "initial_state" }, { "Lua variable", 0 }, false)

map.nodes = {}
local function node_key(index)
    index = math.tointeger(tonumber(index))
    if not index or index < 0 then error("node index must be a non-negative integer", 3) end
    return string.format("%03d", index), index
end
local function node_count(house)
    house = require_registered_id("Houses", house)
    local count = math.tointeger(tonumber(map.get(house, "NodeCount", "0")))
    if not count or count < 0 then error("house has an invalid NodeCount", 3) end
    return count
end
local function encode_node(value, current)
    if value.raw ~= nil then return tostring(value.raw) end
    current = current or {}
    local node_type = value.type ~= nil and tostring(value.type) or current.type
    local x = math.tointeger(tonumber(value.x ~= nil and value.x or current.x))
    local y = math.tointeger(tonumber(value.y ~= nil and value.y or current.y))
    if not node_type or node_type == "" then error("node type is required", 3) end
    if not x or not y or x < 0 or y < 0 or x >= map.info.iso_size or y >= map.info.iso_size then
        error("node coordinates are outside the map buffer", 3)
    end
    return join_csv({ node_type, y, x })
end
function map.nodes.get(house, index)
    local key
    key, index = node_key(index)
    if index >= node_count(house) then return nil end
    local raw = map.get(house, key)
    if raw == nil then return nil end
    local fields = split_csv(raw)
    return {
        id = tostring(index), index = index, key = key, raw = raw, fields = fields,
        type = fields[1], y = tonumber(fields[2]), x = tonumber(fields[3]),
    }
end
function map.nodes.list(house)
    local result = {}
    for index = 0, node_count(house) - 1 do
        local value = map.nodes.get(house, index)
        if value then result[#result + 1] = value end
    end
    return result
end
function map.nodes.create(house, value)
    value = value or {}
    local count = node_count(house)
    if value.id ~= nil or value.index ~= nil then
        local requested = math.tointeger(tonumber(value.id or value.index))
        if requested ~= count then error("new nodes must be appended at NodeCount", 2) end
    end
    local key = node_key(count)
    map.set(house, key, encode_node(value))
    map.set(house, "NodeCount", count + 1)
    return tostring(count)
end
function map.nodes.update(house, index, changes)
    local current = map.nodes.get(house, index)
    if not current then error("node does not exist", 2) end
    changes = changes or {}
    map.set(house, current.key, encode_node(changes, current))
    return tostring(current.index)
end
function map.nodes.move(house, index, x, y)
    return map.nodes.update(house, index, { x = x, y = y })
end
function map.nodes.delete(house, index)
    local key
    key, index = node_key(index)
    local count = node_count(house)
    if index >= count or not map.has(house, key) then return false end
    for current = index, count - 2 do
        local destination = node_key(current)
        local source = node_key(current + 1)
        map.set(house, destination, map.get(house, source, ""))
    end
    local last_key = node_key(count - 1)
    map.remove(house, last_key)
    map.set(house, "NodeCount", count - 1)
    return true
end

map.tubes = {
    get = function(id) return map.records.get("Tubes", id) end,
    list = function() return map.records.list("Tubes") end,
    create = function(value, id) return map.records.create("Tubes", value, id) end,
    update = function(id, value) return map.records.update("Tubes", id, value) end,
    delete = function(id) return map.records.delete("Tubes", id) end,
}

map.cells = {}
function map.cells.get(x, y)
    local fields = split_csv(editor.invoke("map.cell.get", x, y))
    return {
        x = tonumber(fields[1]), y = tonumber(fields[2]), tile = tonumber(fields[3]),
        subtile = tonumber(fields[4]), height = tonumber(fields[5]), map_data = tonumber(fields[6]),
        map_data2 = tonumber(fields[7]), random_image = tonumber(fields[8]),
    }
end
function map.cells.set(x, y, value)
    value = value or {}
    local current = map.cells.get(x, y)
    for key, item in pairs(value) do current[key] = item end
    editor.invoke("map.cell.set", x, y, current.tile, current.subtile, current.height,
        current.map_data, current.map_data2, current.random_image)
end

map.overlay = {}
function map.overlay.get(x, y)
    local fields = split_csv(editor.invoke("map.overlay.get", x, y))
    return { type = tonumber(fields[1]), data = tonumber(fields[2]) }
end
function map.overlay.set(x, y, overlay_type, overlay_data)
    editor.invoke("map.overlay.set", x, y, overlay_type, overlay_data or 0)
end
function map.overlay.clear() editor.invoke("map.overlay.clear") end

map.terrain = {}
function map.terrain.list()
    local result = {}
    for _, raw in ipairs(list_result("map.terrain.list")) do
        local fields = split_csv(raw)
        result[#result + 1] = { index = tonumber(fields[1]), type = fields[2], x = tonumber(fields[3]), y = tonumber(fields[4]) }
    end
    return result
end
function map.terrain.create(value)
    return tonumber(editor.invoke("map.terrain.create", value.type, value.x, value.y))
end
function map.terrain.get(index)
    for _, value in ipairs(map.terrain.list()) do if value.index == index then return value end end
    return nil
end
function map.terrain.update(index, changes)
    local current = map.terrain.get(index)
    if not current then error("terrain index does not exist", 2) end
    for key, value in pairs(changes or {}) do current[key] = value end
    editor.invoke("map.terrain.update", index, current.type, current.x, current.y)
    return index
end
function map.terrain.move(index, x, y) return map.terrain.update(index, { x = x, y = y }) end
function map.terrain.delete(index) editor.invoke("map.terrain.delete", index) end
function map.terrain.smooth(x, y) editor.invoke("map.terrain.smooth", x, y) end
function map.terrain.smooth_tiberium(x, y) editor.invoke("map.terrain.smooth_tiberium", x, y) end
function map.terrain.create_shore(left, top, right, bottom, remove_useless)
    editor.invoke("map.terrain.create_shore", left, top, right, bottom, remove_useless == false and 0 or 1)
end
function map.terrain.auto_level() editor.invoke("map.terrain.auto_level") end

map.smudges = {}
function map.smudges.list()
    local result = {}
    for _, raw in ipairs(list_result("map.smudge.list")) do
        local fields = split_csv(raw)
        result[#result + 1] = { index = tonumber(fields[1]), type = fields[2], x = tonumber(fields[3]), y = tonumber(fields[4]) }
    end
    return result
end
function map.smudges.create(value)
    return tonumber(editor.invoke("map.smudge.create", value.type, value.x, value.y))
end
function map.smudges.get(index)
    for _, value in ipairs(map.smudges.list()) do if value.index == index then return value end end
    return nil
end
function map.smudges.update(index, changes)
    local current = map.smudges.get(index)
    if not current then error("smudge index does not exist", 2) end
    for key, value in pairs(changes or {}) do current[key] = value end
    editor.invoke("map.smudge.update", index, current.type, current.x, current.y)
    return index
end
function map.smudges.move(index, x, y) return map.smudges.update(index, { x = x, y = y }) end
function map.smudges.delete(index) editor.invoke("map.smudge.delete", index) end

map.objects.terrain = map.terrain
map.objects.smudges = map.smudges

function map.resize(left, top, width, height)
    editor.invoke("map.resize", left, top, width, height)
end
function map.set_theater(theater)
    editor.invoke("map.theater.set", theater)
end

map.metrics = {}
function map.metrics.money() return tonumber(editor.invoke("map.metrics.money")) end
function map.metrics.power(house) return tonumber(editor.invoke("map.metrics.power", house)) end

game = {}
function game.get(source, section, key, default)
    local value = editor.invoke("game.get", source, section, key)
    if value == "$lua-missing$" then return default end
    return value
end
function game.has(source, section, key) return game.get(source, section, key, nil) ~= nil end
function game.sections(source) return list_result("game.sections", source) end
function game.keys(source, section) return list_result("game.keys", source, section) end
function game.section(source, section)
    local result = {}
    for _, key in ipairs(game.keys(source, section)) do result[key] = game.get(source, section, key) end
    return result
end
game.rules = { get = function(section, key, default) return game.get("rules", section, key, default) end,
    keys = function(section) return game.keys("rules", section) end,
    section = function(section) return game.section("rules", section) end }
game.art = { get = function(section, key, default) return game.get("art", section, key, default) end,
    keys = function(section) return game.keys("art", section) end,
    section = function(section) return game.section("art", section) end }
game.csf = {}
function game.csf.get(id, default)
    local value = editor.invoke("game.csf.get", id)
    if value == "$lua-missing$" then return default end
    return value
end

ui = {}
function ui.message(text, title) editor.invoke("ui.message", title or "Lua map script", text) end
function ui.confirm(text, title) return editor.invoke("ui.confirm", title or "Lua map script", text) == "1" end
function ui.input(text, title) return editor.invoke("ui.input", title or "Lua map script", text) end
function ui.select(options, text, title)
    local args = { "ui.select", title or "Lua map script", text or "Select a value" }
    for _, option in ipairs(options or {}) do args[#args + 1] = option end
    return editor.invoke(table.unpack(args))
end
function ui.input_integer(text, minimum, maximum, title)
    while true do
        local raw = ui.input(text, title)
        if raw == "" then return nil end
        local value = math.tointeger(tonumber(raw))
        if value and (minimum == nil or value >= minimum) and (maximum == nil or value <= maximum) then
            return value
        end
        ui.message("Please enter a valid integer in the requested range.", title)
    end
end
local function select_ids(records, text, title)
    local options = {}
    for _, record in ipairs(records) do options[#options + 1] = record.id end
    return ui.select(options, text, title)
end
function ui.select_house(text, title) return select_ids(map.houses.list(), text or "Select a house", title) end
function ui.select_country(text, title) return select_ids(map.countries.list(), text or "Select a country", title) end
function ui.select_trigger(text, title)
    local records = {}
    for _, trigger in ipairs(map.triggers.list()) do records[#records + 1] = { id = trigger.id } end
    return select_ids(records, text or "Select a trigger", title)
end
function ui.select_tag(text, title)
    local records = {}
    for _, tag in ipairs(map.tags.list()) do records[#records + 1] = { id = tag.id } end
    return select_ids(records, text or "Select a tag", title)
end
function map.require_multiplayer()
    if not map.info.multiplayer then error("this script requires a multiplayer map", 2) end
end
function map.require_singleplayer()
    if map.info.multiplayer then error("this script requires a single-player map", 2) end
end

editor.capabilities = list_result("capabilities")
function editor.has_capability(name)
    for _, capability in ipairs(editor.capabilities) do
        if capability == name then return true end
    end
    return false
end
function editor.focus(x, y) editor.invoke("editor.focus", x, y) end
function editor.focus_waypoint(id) editor.invoke("editor.focus_waypoint", id) end
function editor.redraw() editor.invoke("editor.redraw") end
