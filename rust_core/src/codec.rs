//! Memory-safe ports of the Westwood pack codecs (base64, Format80,
//! Format5/LZO and the IsoMapPack5 sectioned variants) previously
//! implemented in 3rdParty/xcc/misc/shp_decode.cpp and wrapped by
//! FSunPackLib.
//!
//! The C++ originals walked raw pointers with lengths taken straight from
//! the file being decoded; a corrupt map file could drive them out of
//! bounds and corrupt the heap. These ports are fully bounds-checked:
//!   * decoding is clamped to the input buffer and the caller-provided
//!     output capacity; malformed sections are reported and skipped,
//!   * lookbehind reads in Format80 are clamped to the output buffer
//!     (the C++ code read before the output start on corrupt data),
//!   * section counts/declared sizes are validated before any decode.
//!
//! All exported functions route through `catch_unwind` so a panic can
//! never unwind into C++ code.

use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::{Mutex, OnceLock};

use crate::{RS_ERR_BAD_ARG, RS_ERR_PANIC, RS_ERR_SMALL_BUFFER, RS_OK};

// ---------------------------------------------------------------------------
// Base64 (mirrors xcc encode64/decode64)
// ---------------------------------------------------------------------------

const BASE64_TABLE: &[u8; 64] =
    b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

fn base64_decode_table() -> &'static [i8; 256] {
    static TABLE: OnceLock<[i8; 256]> = OnceLock::new();
    TABLE.get_or_init(|| {
        let mut t = [-1i8; 256];
        for (i, &c) in BASE64_TABLE.iter().enumerate() {
            t[c as usize] = i as i8;
        }
        t
    })
}

pub fn base64_encode(src: &[u8]) -> Vec<u8> {
    let mut out = Vec::with_capacity((src.len() + 2) / 3 * 4);
    let mut r = 0usize;
    while r < src.len() {
        let c1 = src[r] as usize;
        r += 1;
        out.push(BASE64_TABLE[c1 >> 2]);
        let c2 = if r == src.len() { 0 } else { src[r] as usize };
        if r == src.len() {
            out.push(BASE64_TABLE[((c1 & 0x3) << 4) | ((c2 & 0xf0) >> 4)]);
            out.push(b'=');
            out.push(b'=');
            break;
        }
        r += 1;
        out.push(BASE64_TABLE[((c1 & 0x3) << 4) | ((c2 & 0xf0) >> 4)]);
        let c3 = if r == src.len() { 0 } else { src[r] as usize };
        if r == src.len() {
            out.push(BASE64_TABLE[((c2 & 0xf) << 2) | ((c3 & 0xc0) >> 6)]);
            out.push(b'=');
            break;
        }
        r += 1;
        out.push(BASE64_TABLE[((c2 & 0xf) << 2) | ((c3 & 0xc0) >> 6)]);
        out.push(BASE64_TABLE[c3 & 0x3f]);
    }
    out
}

/// Mirrors xcc decode64: stops at a NUL byte, errors on invalid
/// characters, handles '=' padding. Returns the decoded bytes.
pub fn base64_decode(src: &[u8]) -> Result<Vec<u8>, ()> {
    let table = base64_decode_table();
    let mut out = Vec::with_capacity(src.len() / 4 * 3);
    let mut r = 0usize;
    while r < src.len() && src[r] != 0 {
        let c1 = src[r] as usize;
        r += 1;
        if table[c1] == -1 {
            return Err(());
        }
        if r >= src.len() {
            return Err(());
        }
        let c2 = src[r] as usize;
        r += 1;
        if table[c2] == -1 {
            return Err(());
        }
        if r >= src.len() {
            return Err(());
        }
        let c3 = src[r] as usize;
        r += 1;
        if c3 != b'=' as usize && table[c3] == -1 {
            return Err(());
        }
        if r >= src.len() {
            return Err(());
        }
        let c4 = src[r] as usize;
        r += 1;
        if c4 != b'=' as usize && table[c4] == -1 {
            return Err(());
        }
        out.push(((table[c1] as u8) << 2) | ((table[c2] as u8) >> 4));
        if c3 == b'=' as usize {
            break;
        }
        out.push((((table[c2] as u8) << 4) & 0xf0) | ((table[c3] as u8) >> 2));
        if c4 == b'=' as usize {
            break;
        }
        out.push((((table[c3] as u8) << 6) & 0xc0) | (table[c4] as u8));
    }
    Ok(out)
}

// ---------------------------------------------------------------------------
// Format80 (mirrors xcc encode80 / decode80)
// ---------------------------------------------------------------------------

fn write_w(v: usize, out: &mut Vec<u8>) {
    out.push((v & 0xff) as u8);
    out.push((v >> 8) as u8);
}

/// `get_run_length`: run of identical bytes starting at r.
fn get_run_length(src: &[u8], r: usize) -> usize {
    let v = src[r];
    let mut count = 1usize;
    let mut i = r + 1;
    while i < src.len() && src[i] == v {
        count += 1;
        i += 1;
    }
    count
}

/// `get_same`: longest earlier match for position r. Returns the absolute
/// match position and length; ties keep the later position, exactly like
/// the C++ asm version.
fn get_same(src: &[u8], r: usize) -> (usize, usize) {
    let mut p = 0usize;
    let mut cb_p = 0usize;
    let mut b = 0usize;
    while b < r {
        let mut matched = 0usize;
        while r + matched < src.len() && src[b + matched] == src[r + matched] {
            matched += 1;
        }
        if matched >= cb_p {
            cb_p = matched;
            p = b;
        }
        b += 1;
    }
    (p, cb_p)
}

/// write80_c0: short match, count 3..=10, offset r-p < 0x1000.
fn write80_c0(out: &mut Vec<u8>, count: usize, p: usize) {
    out.push((((count - 3) << 4) | (p >> 8)) as u8);
    out.push((p & 0xff) as u8);
}

/// write80_c1: literal run in chunks of 0x3f with 0x80|c_write prefix.
fn write80_c1(out: &mut Vec<u8>, count: usize, src: &[u8]) {
    let mut count = count;
    let mut r = 0usize;
    while count > 0 {
        let c_write = count.min(0x3f);
        out.push(0x80 | c_write as u8);
        out.extend_from_slice(&src[r..r + c_write]);
        r += c_write;
        count -= c_write;
    }
}

/// write80_c2: medium match, count 3..=0x40, absolute position p < 0x10000.
fn write80_c2(out: &mut Vec<u8>, count: usize, p: usize) {
    out.push(0xc0 | (count - 3) as u8);
    write_w(p, out);
}

/// write80_c3: fill with one byte, any count.
fn write80_c3(out: &mut Vec<u8>, count: usize, v: u8) {
    out.push(0xfe);
    write_w(count, out);
    out.push(v);
}

/// write80_c4: long match, any count, absolute position p.
fn write80_c4(out: &mut Vec<u8>, count: usize, p: usize) {
    out.push(0xff);
    write_w(count, out);
    write_w(p, out);
}

/// flush_c1: emit a pending literal run.
fn flush_c1(out: &mut Vec<u8>, r: usize, copy_from: &mut Option<usize>, src: &[u8]) {
    if let Some(cf) = *copy_from {
        write80_c1(out, r - cf, &src[cf..r]);
        *copy_from = None;
    }
}

/// Format80 encoder, a faithful port of xcc's encode80 (full compression).
/// Produces a byte stream any Format80 decoder accepts.
pub fn f80_encode(src: &[u8]) -> Vec<u8> {
    let mut out: Vec<u8> = Vec::with_capacity(src.len() + src.len() / 8 + 16);
    let mut r = 0usize;
    let mut copy_from: Option<usize> = None;
    while r < src.len() {
        let t = get_run_length(src, r);
        let (p, cb_p) = get_same(src, r);
        // Format80 stores back-references as u16 offsets: never emit a
        // match whose position does not fit (the C++ encoder truncated
        // the offset and produced corrupt streams in that case).
        let can_match = cb_p > 2 && p < 0x10000;
        if t < cb_p && can_match {
            flush_c1(&mut out, r, &mut copy_from, src);
            if cb_p - 3 < 8 && r - p < 0x1000 {
                write80_c0(&mut out, cb_p, r - p);
            } else if cb_p - 3 < 0x3e {
                write80_c2(&mut out, cb_p, p);
            } else {
                write80_c4(&mut out, cb_p, p);
            }
            r += cb_p;
        } else {
            if t < 3 {
                if copy_from.is_none() {
                    copy_from = Some(r);
                }
            } else {
                flush_c1(&mut out, r, &mut copy_from, src);
                write80_c3(&mut out, t, src[r]);
            }
            r += t;
        }
    }
    flush_c1(&mut out, r, &mut copy_from, src);
    out.push(0x80); // end of stream
    out
}

/// Format80 decoder writing into `dst[..limit]` (limit <= dst.len()).
/// Returns the number of bytes written. Lookbehind offsets larger than
/// the bytes written so far are clamped to the last written byte instead
/// of reading out of bounds like the C++ code did.
fn f80_decode_to(src: &[u8], dst: &mut [u8], limit: usize) -> Result<usize, ()> {
    let limit = limit.min(dst.len());
    let mut r = 0usize;
    let mut w = 0usize;
    loop {
        if r >= src.len() {
            return Err(());
        }
        let code = src[r];
        r += 1;
        if code & 0x80 == 0 {
            // command 0: copy from w - offset (relative lookbehind)
            if r >= src.len() {
                return Err(());
            }
            let offset = (((code & 0xf) as usize) << 8) + src[r] as usize;
            r += 1;
            let count = ((code >> 4) as usize) + 3;
            for _ in 0..count {
                let v = if w == 0 {
                    0
                } else {
                    dst[w.saturating_sub(offset).min(w - 1)]
                };
                if w >= limit {
                    return Err(());
                }
                dst[w] = v;
                w += 1;
            }
        } else {
            let count = (code & 0x3f) as usize;
            if code & 0x40 == 0 {
                if count == 0 {
                    return Ok(w); // end of image
                }
                // command 1: literal copy from source
                if r + count > src.len() {
                    return Err(());
                }
                for i in 0..count {
                    if w >= limit {
                        return Err(());
                    }
                    dst[w] = src[r + i];
                    w += 1;
                }
                r += count;
            } else if count < 0x3e {
                // command 2: copy from absolute output offset
                if r + 2 > src.len() {
                    return Err(());
                }
                let offset = u16::from_le_bytes([src[r], src[r + 1]]) as usize;
                r += 2;
                let count = count + 3;
                for i in 0..count {
                    let v = if w == 0 { 0 } else { dst[(offset + i).min(w - 1)] };
                    if w >= limit {
                        return Err(());
                    }
                    dst[w] = v;
                    w += 1;
                }
            } else if count == 0x3e {
                // command 3: fill with a byte value
                if r + 3 > src.len() {
                    return Err(());
                }
                let count = u16::from_le_bytes([src[r], src[r + 1]]) as usize;
                let v = src[r + 2];
                r += 3;
                if w + count > limit {
                    return Err(());
                }
                dst[w..w + count].fill(v);
                w += count;
            } else {
                // command 4: long copy from absolute output offset
                if r + 4 > src.len() {
                    return Err(());
                }
                let count = u16::from_le_bytes([src[r], src[r + 1]]) as usize;
                let offset = u16::from_le_bytes([src[r + 2], src[r + 3]]) as usize;
                r += 4;
                for i in 0..count {
                    let v = if w == 0 { 0 } else { dst[(offset + i).min(w - 1)] };
                    if w >= limit {
                        return Err(());
                    }
                    dst[w] = v;
                    w += 1;
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Pack5 / IsoMapPack5 (Format 5: LZO sections, mirrors encode5/decode5
// with format = 5 as used by EncodeIsoMapPack5/DecodeIsoMapPack5)
// ---------------------------------------------------------------------------

const PACK5_SECTION: usize = 8192;

/// Global LZO state: one wrkmem buffer behind a mutex (compression needs
/// exclusive access to the scratch memory).
struct LzoState {
    wrkmem: Box<[u8; crate::minilzo::LZO1X_1_MEM_COMPRESS]>,
}

fn lzo_state() -> &'static Mutex<LzoState> {
    static LZO_STATE: OnceLock<Mutex<LzoState>> = OnceLock::new();
    LZO_STATE.get_or_init(|| {
        crate::minilzo::lzo_init().expect("minilzo init failed");
        Mutex::new(LzoState {
            wrkmem: Box::new([0u8; crate::minilzo::LZO1X_1_MEM_COMPRESS]),
        })
    })
}

pub fn pack5_encode(src: &[u8]) -> Result<Vec<u8>, ()> {
    let mut out = Vec::with_capacity(src.len() + src.len() / 8 + 16);
    let mut r = 0usize;
    while r < src.len() {
        let section_len = (src.len() - r).min(PACK5_SECTION);
        // worst case LZO1X-1 expansion: in + in/16 + 64 + 3
        let mut scratch = vec![0u8; section_len + section_len / 16 + 64 + 3];
        let compressed_len = {
            let mut guard = lzo_state().lock().unwrap_or_else(|e| e.into_inner());
            crate::minilzo::lzo_compress(
                &src[r..r + section_len],
                &mut scratch,
                &mut guard.wrkmem,
            )?
        };
        if compressed_len > 0xFFFF {
            // xcc would silently truncate size_in here, producing a corrupt
            // stream; refuse instead.
            return Err(());
        }
        out.extend_from_slice(&(compressed_len as u16).to_le_bytes());
        out.extend_from_slice(&(section_len as u16).to_le_bytes());
        out.extend_from_slice(&scratch[..compressed_len]);
        r += section_len;
    }
    Ok(out)
}

/// Scans the section headers of a Format5 stream and returns the total
/// decoded size. Errors when the headers run past the end of `src` or
/// the total exceeds `max_size`.
fn pack5_decode_scan(src: &[u8], max_size: usize) -> Result<usize, ()> {
    let mut r = 0usize;
    let mut total = 0usize;
    while r < src.len() {
        if r + 4 > src.len() {
            return Err(());
        }
        let size_in = u16::from_le_bytes([src[r], src[r + 1]]) as usize;
        let size_out = u16::from_le_bytes([src[r + 2], src[r + 3]]) as usize;
        r += 4;
        if r + size_in > src.len() {
            return Err(());
        }
        r += size_in;
        total = total.checked_add(size_out).ok_or(())?;
        if total > max_size {
            return Err(());
        }
    }
    Ok(total)
}

/// Decodes a Format5 stream into `dst` (which must be at least `needed`
/// bytes, as returned by `pack5_decode_scan`). Returns the number of
/// bytes written.
fn pack5_decode_to(src: &[u8], dst: &mut [u8], needed: usize) -> Result<usize, ()> {
    if dst.len() < needed {
        return Err(());
    }
    let mut r = 0usize;
    let mut w = 0usize;
    while r < src.len() {
        if r + 4 > src.len() {
            return Err(());
        }
        let size_in = u16::from_le_bytes([src[r], src[r + 1]]) as usize;
        let size_out = u16::from_le_bytes([src[r + 2], src[r + 3]]) as usize;
        r += 4;
        if r + size_in > src.len() || w + size_out > dst.len() {
            return Err(());
        }
        if size_in == 0 {
            // empty section (mirrors lzo1x_decompress on 0 input)
            w += size_out;
            continue;
        }
        let mut scratch = vec![0u8; size_out];
        let actual = {
            let _guard = lzo_state().lock().unwrap_or_else(|e| e.into_inner());
            crate::minilzo::lzo_decompress_safe(&src[r..r + size_in], &mut scratch)?
        };
        if actual != size_out {
            // Declared and actual sizes disagree; the C++ code would have
            // corrupted the output layout here. Treat as malformed.
            return Err(());
        }
        dst[w..w + size_out].copy_from_slice(&scratch);
        r += size_in;
        w += size_out;
    }
    Ok(w)
}

// ---------------------------------------------------------------------------
// F80 pack (mirrors FSunPackLib::EncodeF80/DecodeF80; sections of
// len / n_sections bytes with the 4-byte section header)
// ---------------------------------------------------------------------------

pub fn f80_pack_encode(src: &[u8], n_sections: usize) -> Result<Vec<u8>, ()> {
    if n_sections == 0 {
        return Err(());
    }
    let length = src.len() / n_sections;
    if length == 0 {
        return Err(());
    }
    let mut out = Vec::with_capacity(src.len() + src.len() / 8 + n_sections * 4);
    for i in 0..n_sections {
        let packed = f80_encode(&src[i * length..(i + 1) * length]);
        if packed.len() >= 0x10000 {
            // section header cannot represent this size (and the byte
            // written at offset 3 would corrupt size_out)
            return Err(());
        }
        out.push((packed.len() & 0xff) as u8);
        out.push(((packed.len() >> 8) & 0xff) as u8);
        out.push(((packed.len() >> 16) & 0xff) as u8);
        out.push(0x20);
        out.extend_from_slice(&packed);
    }
    Ok(out)
}

fn f80_pack_decode_scan(src: &[u8], max_size: usize) -> Result<usize, ()> {
    let mut r = 0usize;
    let mut total = 0usize;
    while r < src.len() {
        if r + 4 > src.len() {
            return Err(());
        }
        let size_in = u16::from_le_bytes([src[r], src[r + 1]]) as usize;
        let size_out = u16::from_le_bytes([src[r + 2], src[r + 3]]) as usize;
        r += 4;
        if r + size_in > src.len() {
            return Err(());
        }
        r += size_in;
        total = total.checked_add(size_out).ok_or(())?;
        if total > max_size {
            return Err(());
        }
    }
    Ok(total)
}

fn f80_pack_decode_to(src: &[u8], dst: &mut [u8], needed: usize) -> Result<usize, ()> {
    if dst.len() < needed {
        return Err(());
    }
    let mut r = 0usize;
    let mut w = 0usize;
    while r < src.len() {
        if r + 4 > src.len() {
            return Err(());
        }
        let size_in = u16::from_le_bytes([src[r], src[r + 1]]) as usize;
        let size_out = u16::from_le_bytes([src[r + 2], src[r + 3]]) as usize;
        r += 4;
        if r + size_in > src.len() || w + size_out > dst.len() {
            return Err(());
        }
        let written = f80_decode_to(&src[r..r + size_in], &mut dst[w..], size_out)?;
        if written != size_out {
            return Err(());
        }
        r += size_in;
        w += size_out;
    }
    Ok(w)
}

// ---------------------------------------------------------------------------
// C ABI
// ---------------------------------------------------------------------------

fn copy_or_measure(src: &[u8], dst: *mut u8, dst_cap: usize, out_len: *mut usize) -> i32 {
    unsafe {
        if !out_len.is_null() {
            *out_len = src.len();
        }
    }
    if dst.is_null() || dst_cap < src.len() {
        return RS_ERR_SMALL_BUFFER;
    }
    unsafe {
        std::ptr::copy_nonoverlapping(src.as_ptr(), dst, src.len());
    }
    RS_OK
}

/// Encodes `src` as base64. The output is NOT NUL-terminated; the C++
/// wrapper appends the terminator exactly like the original code.
#[no_mangle]
pub unsafe extern "C" fn rs_base64_encode(
    src: *const u8,
    src_len: usize,
    dst: *mut u8,
    dst_cap: usize,
    out_len: *mut usize,
) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        if src.is_null() || src_len == 0 {
            return RS_ERR_BAD_ARG;
        }
        let src = std::slice::from_raw_parts(src, src_len);
        let encoded = base64_encode(src);
        copy_or_measure(&encoded, dst, dst_cap, out_len)
    }))
    .unwrap_or(RS_ERR_PANIC)
}

/// Decodes a NUL-terminated base64 string (decoding stops at the first
/// NUL byte, mirroring xcc's `while (*r)` loop).
#[no_mangle]
pub unsafe extern "C" fn rs_base64_decode(
    src: *const u8,
    src_len: usize,
    dst: *mut u8,
    dst_cap: usize,
    out_len: *mut usize,
) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        if src.is_null() || src_len == 0 {
            return RS_ERR_BAD_ARG;
        }
        let src = std::slice::from_raw_parts(src, src_len);
        let decoded = match base64_decode(src) {
            Ok(d) => d,
            Err(()) => return RS_ERR_BAD_ARG,
        };
        copy_or_measure(&decoded, dst, dst_cap, out_len)
    }))
    .unwrap_or(RS_ERR_PANIC)
}

/// Raw Format80 encode (mirrors xcc encode80 / FSunPackLib::ConvertToF80).
#[no_mangle]
pub unsafe extern "C" fn rs_f80_encode(
    src: *const u8,
    src_len: usize,
    dst: *mut u8,
    dst_cap: usize,
    out_len: *mut usize,
) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        if src.is_null() || src_len == 0 {
            return RS_ERR_BAD_ARG;
        }
        let src = std::slice::from_raw_parts(src, src_len);
        let encoded = f80_encode(src);
        copy_or_measure(&encoded, dst, dst_cap, out_len)
    }))
    .unwrap_or(RS_ERR_PANIC)
}

/// Encodes with the sectioned Format80 layout used by
/// FSunPackLib::EncodeF80 (n_sections sections of len/n_sections bytes).
#[no_mangle]
pub unsafe extern "C" fn rs_f80_pack_encode(
    src: *const u8,
    src_len: usize,
    n_sections: u32,
    dst: *mut u8,
    dst_cap: usize,
    out_len: *mut usize,
) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        if src.is_null() || src_len == 0 {
            return RS_ERR_BAD_ARG;
        }
        let src = std::slice::from_raw_parts(src, src_len);
        let encoded = match f80_pack_encode(src, n_sections as usize) {
            Ok(e) => e,
            Err(()) => return RS_ERR_BAD_ARG,
        };
        copy_or_measure(&encoded, dst, dst_cap, out_len)
    }))
    .unwrap_or(RS_ERR_PANIC)
}

/// Decodes the sectioned Format80 layout (FSunPackLib::DecodeF80).
/// `max_size` caps the declared output size (mirrors the original check).
/// Pass `dst` = NULL / `dst_cap` = 0 to only measure.
#[no_mangle]
pub unsafe extern "C" fn rs_f80_pack_decode(
    src: *const u8,
    src_len: usize,
    dst: *mut u8,
    dst_cap: usize,
    max_size: usize,
    out_len: *mut usize,
) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        if src.is_null() || src_len == 0 {
            return RS_ERR_BAD_ARG;
        }
        let src = std::slice::from_raw_parts(src, src_len);
        let needed = match f80_pack_decode_scan(src, max_size) {
            Ok(n) => n,
            Err(()) => return RS_ERR_BAD_ARG,
        };
        unsafe {
            if !out_len.is_null() {
                *out_len = needed;
            }
        }
        if dst.is_null() || dst_cap < needed {
            return RS_ERR_SMALL_BUFFER;
        }
        let dst = std::slice::from_raw_parts_mut(dst, dst_cap);
        match f80_pack_decode_to(src, dst, needed) {
            Ok(_) => RS_OK,
            Err(()) => RS_ERR_BAD_ARG,
        }
    }))
    .unwrap_or(RS_ERR_PANIC)
}

/// Encodes with the Format5/LZO layout used by
/// FSunPackLib::EncodeIsoMapPack5 (8192-byte sections).
#[no_mangle]
pub unsafe extern "C" fn rs_pack5_encode(
    src: *const u8,
    src_len: usize,
    dst: *mut u8,
    dst_cap: usize,
    out_len: *mut usize,
) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        if src.is_null() || src_len == 0 {
            return RS_ERR_BAD_ARG;
        }
        let src = std::slice::from_raw_parts(src, src_len);
        let encoded = match pack5_encode(src) {
            Ok(e) => e,
            Err(()) => return RS_ERR_BAD_ARG,
        };
        copy_or_measure(&encoded, dst, dst_cap, out_len)
    }))
    .unwrap_or(RS_ERR_PANIC)
}

/// Decodes the Format5/LZO layout (FSunPackLib::DecodeIsoMapPack5).
/// `max_size` caps the declared output size; sections that would overflow
/// are rejected instead of corrupting the heap. Pass dst = NULL /
/// dst_cap = 0 to only measure.
#[no_mangle]
pub unsafe extern "C" fn rs_pack5_decode(
    src: *const u8,
    src_len: usize,
    dst: *mut u8,
    dst_cap: usize,
    max_size: usize,
    out_len: *mut usize,
) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        if src.is_null() || src_len == 0 {
            return RS_ERR_BAD_ARG;
        }
        let src = std::slice::from_raw_parts(src, src_len);
        let needed = match pack5_decode_scan(src, max_size) {
            Ok(n) => n,
            Err(()) => return RS_ERR_BAD_ARG,
        };
        unsafe {
            if !out_len.is_null() {
                *out_len = needed;
            }
        }
        if dst.is_null() || dst_cap < needed {
            return RS_ERR_SMALL_BUFFER;
        }
        let dst = std::slice::from_raw_parts_mut(dst, dst_cap);
        match pack5_decode_to(src, dst, needed) {
            Ok(_) => RS_OK,
            Err(()) => RS_ERR_BAD_ARG,
        }
    }))
    .unwrap_or(RS_ERR_PANIC)
}

// ---------------------------------------------------------------------------
// Unit tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn base64_roundtrip() {
        let data = b"the quick brown fox jumps over the lazy dog";
        let encoded = base64_encode(data);
        assert_eq!(
            &encoded[..],
            b"dGhlIHF1aWNrIGJyb3duIGZveCBqdW1wcyBvdmVyIHRoZSBsYXp5IGRvZw=="
        );
        let decoded = base64_decode(&encoded).unwrap();
        assert_eq!(&decoded[..], &data[..]);
    }

    #[test]
    fn base64_matches_xcc_layout() {
        // xcc encode64 output for 3-byte, 2-byte and 1-byte tails
        let a = base64_encode(&[0x12, 0x34, 0x56]);
        assert_eq!(&a[..], b"EjRW");
        let b = base64_encode(&[0x12, 0x34]);
        assert_eq!(&b[..], b"EjQ=");
        let c = base64_encode(&[0x12]);
        assert_eq!(&c[..], b"Eg==");
    }

    #[test]
    fn base64_decode_rejects_invalid() {
        assert!(base64_decode(b"a*bb").is_err());
        // NUL terminates the string
        let d = base64_decode(b"EjRW\0garbage").unwrap();
        assert_eq!(&d[..], &[0x12, 0x34, 0x56]);
    }

    #[test]
    fn f80_roundtrip_various_data() {
        let cases: Vec<Vec<u8>> = vec![
            b"hello world".to_vec(),
            vec![7u8; 10000],
            (0..8192).map(|i| (i % 251) as u8).collect(),
            (0..300).map(|i| (i * i % 17) as u8).collect(),
            vec![0u8; 8192],
            vec![255u8; 9000],
            b"AAAAABBBBBCCCCCDDDDD".to_vec(),
        ];
        for data in cases {
            let encoded = f80_encode(&data);
            let mut decoded = vec![0u8; data.len()];
            let written = f80_decode_to(&encoded, &mut decoded, data.len()).unwrap();
            assert_eq!(written, data.len(), "decoded length mismatch");
            assert_eq!(&decoded[..], &data[..], "data mismatch");
        }
    }

    #[test]
    fn f80_decode_bounded_on_truncated_input() {
        let data = (0..4000).map(|i| (i % 251) as u8).collect::<Vec<_>>();
        let encoded = f80_encode(&data);
        let mut decoded = vec![0u8; data.len()];
        // truncated stream must not panic; result may be Ok (stopped at
        // terminator found early) or Err - either way the buffer is intact
        let _ = f80_decode_to(&encoded[..encoded.len() / 2], &mut decoded, data.len());
        // a tiny output limit must not write out of bounds
        let mut small = [0xAAu8; 16];
        let _ = f80_decode_to(&encoded, &mut small, 16);
        assert!(small.iter().all(|&b| b != 0xAA) || small[0] != 0xAA);
    }

    #[test]
    fn f80_pack_roundtrip() {
        let data = vec![42u8; 262144];
        let packed = f80_pack_encode(&data, 32).unwrap();
        let needed = f80_pack_decode_scan(&packed, 1 << 20).unwrap();
        assert_eq!(needed, 262144);
        let mut out = vec![0u8; needed];
        let written = f80_pack_decode_to(&packed, &mut out, needed).unwrap();
        assert_eq!(written, needed);
        assert_eq!(&out[..], &data[..]);
    }

    #[test]
    fn pack5_roundtrip() {
        // map field data sized like a 200x200 map
        let mut data = vec![0u8; 200 * 200 * 11];
        for i in 0..data.len() {
            data[i] = (i % 47) as u8;
        }
        let packed = pack5_encode(&data).unwrap();
        let needed = pack5_decode_scan(&packed, 1 << 30).unwrap();
        assert_eq!(needed, data.len());
        let mut out = vec![0u8; needed];
        let written = pack5_decode_to(&packed, &mut out, needed).unwrap();
        assert_eq!(written, needed);
        assert_eq!(&out[..], &data[..]);
    }

    #[test]
    fn pack5_rejects_corrupt_headers() {
        // header declares more input than present
        let src = [0x10u8, 0x00, 0x00, 0x08];
        assert!(pack5_decode_scan(&src, 1 << 30).is_err());
        // headers declare oversized output
        let src = [0x00u8, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF];
        assert!(pack5_decode_scan(&src, 1 << 16).is_err());
    }

    #[test]
    fn f80_encode_runs_and_terminates() {
        // cover the run/literal/match code paths of the encoder
        let mut data = Vec::new();
        data.extend_from_slice(b"ABCDEFGHIJKLMNOPQRSTUVWXYZ");
        data.extend_from_slice(b"ABCDEFGHIJKLMNOPQRSTUVWXYZ");
        data.extend_from_slice(&[9u8; 5000]);
        data.extend_from_slice(b"long match long match long match long match");
        data.extend_from_slice(&[9u8; 3]);
        let encoded = f80_encode(&data);
        let mut decoded = vec![0u8; data.len()];
        let written = f80_decode_to(&encoded, &mut decoded, data.len()).unwrap();
        assert_eq!(written, data.len());
        assert_eq!(&decoded[..], &data[..]);
    }
}
