//! Memory-safe RA2/YR CSF (string table) parser.
//!
//! The C++ original (CLoading::LoadStrings) walked the file with a raw
//! pointer, trusted every length field, and allocated
//! `new BYTE[dwCharCount + 1]` before copying that many bytes - a corrupt
//! file could read far past the end of the buffer and corrupt the heap.
//!
//! This parser clamps every read to the input buffer, caps the entry
//! count by how much data actually exists, and copies nothing until the
//! declared byte ranges have been validated. Entries are packed into
//! caller-provided flat buffers (ids / decoded UTF-16 values / ascii
//! values) so the C++ side does not need per-entry allocations either.

use std::panic::{catch_unwind, AssertUnwindSafe};

use crate::{RS_ERR_BAD_ARG, RS_ERR_PANIC, RS_ERR_SMALL_BUFFER, RS_OK};

/// One parsed string table entry. The byte blobs are laid out in the same
/// order in the `ids` / `values` / `values_asc` output buffers.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct RsCsfEntry {
    /// length of the id in bytes (ASCII)
    pub id_len: u32,
    /// length of the value in UTF-16 code units (WCHARs)
    pub value_len: u32,
    /// length of the extra ascii value in bytes (0 when absent)
    pub value_asc_len: u32,
}

struct ParseResult {
    entries: Vec<RsCsfEntry>,
    ids: Vec<u8>,
    values: Vec<u8>,
    values_asc: Vec<u8>,
    truncated: bool,
}

/// Parses the whole CSF file. Every read is bounds-checked against
/// `data`; a malformed file yields as many valid entries as could be
/// recovered (with `truncated` set) instead of crashing.
fn parse_csf(data: &[u8]) -> Option<ParseResult> {
    // locate " FSC"
    let mut pos = data.windows(4).position(|w| w == b" FSC")?;
    pos += 4;

    if pos + 20 > data.len() {
        return None;
    }
    let dw_count1 = u32::from_le_bytes([data[pos + 4], data[pos + 5], data[pos + 6], data[pos + 7]]);
    pos += 20;

    let mut entries = Vec::new();
    let mut ids = Vec::new();
    let mut values = Vec::new();
    let mut values_asc = Vec::new();
    let mut truncated = false;

    for _ in 0..dw_count1 {
        // Real CSF label records start with " LBL", followed by the number
        // of strings assigned to the label and the label length. The old
        // parser skipped only eight bytes here, so it interpreted the string
        // count as the label length and could not parse an actual RA2/YR CSF.
        if pos + 12 > data.len() || &data[pos..pos + 4] != b" LBL" {
            truncated = true;
            break;
        }
        let string_count = u32::from_le_bytes([
            data[pos + 4],
            data[pos + 5],
            data[pos + 6],
            data[pos + 7],
        ]) as usize;
        let id_len = u32::from_le_bytes([
            data[pos + 8],
            data[pos + 9],
            data[pos + 10],
            data[pos + 11],
        ]) as usize;
        pos += 12;
        if pos + id_len > data.len() {
            truncated = true;
            break;
        }
        let id = &data[pos..pos + id_len];
        pos += id_len;

        // Even an empty string record consumes an eight-byte marker/length
        // header. Cap the count by the remaining bytes before iterating so a
        // corrupt count cannot turn into an unbounded loop.
        if string_count > (data.len().saturating_sub(pos) / 8) {
            truncated = true;
            break;
        }

        let mut first_value = Vec::new();
        let mut first_value_len = 0usize;
        let mut first_value_asc = Vec::new();
        let mut label_complete = true;
        for string_index in 0..string_count {
            if pos + 8 > data.len() {
                truncated = true;
                label_complete = false;
                break;
            }

            let marker = &data[pos..pos + 4];
            let has_extra_value = marker == b"WRTS";
            if marker != b" RTS" && !has_extra_value {
                truncated = true;
                label_complete = false;
                break;
            }
            pos += 4;

            let value_len = u32::from_le_bytes([
                data[pos],
                data[pos + 1],
                data[pos + 2],
                data[pos + 3],
            ]) as usize;
            pos += 4;
            if value_len > usize::MAX / 2 || pos + value_len * 2 > data.len() {
                truncated = true;
                label_complete = false;
                break;
            }

            // Values are stored as ~UTF-16LE (each code unit XORed with ~).
            // FA2 consumes the first string when a label contains variants;
            // still walk later strings so the following label stays aligned.
            if string_index == 0 {
                first_value_len = value_len;
                first_value.reserve(value_len * 2);
                for c in data[pos..pos + value_len * 2].chunks_exact(2) {
                    let w = u16::from_le_bytes([c[0], c[1]]);
                    first_value.extend_from_slice(&(!w).to_le_bytes());
                }
            }
            pos += value_len * 2;

            if has_extra_value {
                if pos + 4 > data.len() {
                    truncated = true;
                    label_complete = false;
                    break;
                }
                let value_asc_len = u32::from_le_bytes([
                    data[pos],
                    data[pos + 1],
                    data[pos + 2],
                    data[pos + 3],
                ]) as usize;
                pos += 4;
                if pos + value_asc_len > data.len() {
                    truncated = true;
                    label_complete = false;
                    break;
                }
                if string_index == 0 {
                    first_value_asc.extend_from_slice(&data[pos..pos + value_asc_len]);
                }
                pos += value_asc_len;
            }
        }

        if !label_complete {
            break;
        }

        ids.extend_from_slice(id);
        values.extend_from_slice(&first_value);
        values_asc.extend_from_slice(&first_value_asc);
        entries.push(RsCsfEntry {
            id_len: id_len as u32,
            value_len: first_value_len as u32,
            value_asc_len: first_value_asc.len() as u32,
        });
    }

    Some(ParseResult {
        entries,
        ids,
        values,
        values_asc,
        truncated,
    })
}

fn copy_blob(blob: &[u8], dst: *mut u8, dst_cap: usize, out_len: *mut usize) -> bool {
    unsafe {
        if !out_len.is_null() {
            *out_len = blob.len();
        }
    }
    if blob.is_empty() {
        return true; // nothing to copy: a NULL dst is fine
    }
    if dst.is_null() || dst_cap < blob.len() {
        return false;
    }
    unsafe {
        std::ptr::copy_nonoverlapping(blob.as_ptr(), dst, blob.len());
    }
    true
}

/// Parses a CSF string table into caller-provided flat buffers.
///
/// Call once with NULL buffers to obtain the required sizes, then again
/// with suitably sized buffers. Returns RS_OK, RS_ERR_SMALL_BUFFER (sizes
/// have been updated) or RS_ERR_BAD_ARG (" FSC" missing / file too short
/// to contain a header).
#[no_mangle]
pub unsafe extern "C" fn rs_csf_parse(
    data: *const u8,
    data_len: usize,
    entries: *mut RsCsfEntry,
    entry_cap: usize,
    out_entry_count: *mut usize,
    ids: *mut u8,
    ids_cap: usize,
    out_ids_len: *mut usize,
    values: *mut u8,
    values_cap: usize,
    out_values_len: *mut usize,
    values_asc: *mut u8,
    values_asc_cap: usize,
    out_values_asc_len: *mut usize,
    out_truncated: *mut i32,
) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        if data.is_null() {
            return RS_ERR_BAD_ARG;
        }
        let data = std::slice::from_raw_parts(data, data_len);
        let parsed = match parse_csf(data) {
            Some(p) => p,
            None => return RS_ERR_BAD_ARG,
        };

        unsafe {
            if !out_entry_count.is_null() {
                *out_entry_count = parsed.entries.len();
            }
            if !out_truncated.is_null() {
                *out_truncated = if parsed.truncated { 1 } else { 0 };
            }
        }

        let mut status = RS_OK;
        if entries.is_null() || entry_cap < parsed.entries.len() {
            if !parsed.entries.is_empty() {
                status = RS_ERR_SMALL_BUFFER;
            }
        } else {
            unsafe {
                std::ptr::copy_nonoverlapping(
                    parsed.entries.as_ptr(),
                    entries,
                    parsed.entries.len(),
                );
            }
        }

        if !copy_blob(&parsed.ids, ids, ids_cap, out_ids_len) {
            status = RS_ERR_SMALL_BUFFER;
        }
        if !copy_blob(&parsed.values, values, values_cap, out_values_len) {
            status = RS_ERR_SMALL_BUFFER;
        }
        if !copy_blob(&parsed.values_asc, values_asc, values_asc_cap, out_values_asc_len) {
            status = RS_ERR_SMALL_BUFFER;
        }

        status
    }))
    .unwrap_or(RS_ERR_PANIC)
}

// ---------------------------------------------------------------------------
// Unit tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    fn build_csf(entries: &[(String, String, Option<String>)]) -> Vec<u8> {
        let mut out = Vec::new();
        out.extend_from_slice(b"TDFT\x00\x00\x00\x00"); // placeholder, filled below
        let mut body = Vec::new();
        for (id, value, asc) in entries {
            body.extend_from_slice(b" LBL");
            body.extend_from_slice(&1u32.to_le_bytes()); // strings for this label
            body.extend_from_slice(&(id.len() as u32).to_le_bytes());
            body.extend_from_slice(id.as_bytes());
            // 4-byte marker: 'W' means an extra ascii value follows
            if asc.is_some() {
                body.extend_from_slice(b"WRTS");
            } else {
                body.extend_from_slice(b" RTS");
            }
            let encoded: Vec<u8> = value
                .encode_utf16()
                .flat_map(|w| (!w).to_le_bytes())
                .collect();
            body.extend_from_slice(&(value.encode_utf16().count() as u32).to_le_bytes());
            body.extend_from_slice(&encoded);
            if let Some(a) = asc {
                body.extend_from_slice(&(a.len() as u32).to_le_bytes());
                body.extend_from_slice(a.as_bytes());
            }
        }
        // header: " FSC" marker before it, then 20 bytes; count1 at +4
        out.extend_from_slice(b" FSC");
        let mut head = Vec::new();
        head.extend_from_slice(&1u32.to_le_bytes()); // dwFlag1
        head.extend_from_slice(&(entries.len() as u32).to_le_bytes()); // dwCount1
        head.extend_from_slice(&0u32.to_le_bytes());
        head.extend_from_slice(&0u32.to_le_bytes());
        head.extend_from_slice(&0u32.to_le_bytes());
        out.extend_from_slice(&head);
        out.extend_from_slice(&body);
        out
    }

    #[test]
    fn parses_entries_roundtrip() {
        let data = build_csf(&[
            ("Name:ABC".into(), "Unit ABC".into(), Some("ascii abc".into())),
            ("Name:XYZ".into(), "Ünït".into(), None),
        ]);
        let parsed = parse_csf(&data).unwrap();
        assert_eq!(parsed.entries.len(), 2);
        assert!(!parsed.truncated);

        assert_eq!(parsed.entries[0].id_len, 8);
        assert_eq!(parsed.entries[0].value_len, 8);
        assert_eq!(parsed.entries[0].value_asc_len, 9);
        assert_eq!(&parsed.ids[..8], b"Name:ABC");
        // decode value back
        let val: Vec<u16> = parsed.values[..16]
            .chunks_exact(2)
            .map(|c| u16::from_le_bytes([c[0], c[1]]))
            .collect();
        assert_eq!(String::from_utf16_lossy(&val), "Unit ABC");
        assert_eq!(&parsed.values_asc[..9], b"ascii abc");

        assert_eq!(parsed.entries[1].value_len, 4);
        assert_eq!(parsed.entries[1].value_asc_len, 0);
    }

    #[test]
    fn stops_on_truncated_data() {
        let data = build_csf(&[
            ("A".into(), "a".into(), None),
            ("B".into(), "b".into(), None),
        ]);
        // cut the file in the middle of the second entry
        let cut = &data[..data.len() - 3];
        let parsed = parse_csf(cut).unwrap();
        assert_eq!(parsed.entries.len(), 1);
        assert!(parsed.truncated);
    }

    #[test]
    fn rejects_missing_marker() {
        let data = b"garbage data without marker";
        assert!(parse_csf(data).is_none());
    }

    #[test]
    fn rejects_declared_size_past_end() {
        // valid marker + header declaring 1 entry, but the entry
        // declares an id longer than the file
        let mut data = Vec::new();
        data.extend_from_slice(b" FSC");
        data.extend_from_slice(&1u32.to_le_bytes());
        data.extend_from_slice(&1u32.to_le_bytes());
        data.extend_from_slice(&0u32.to_le_bytes());
        data.extend_from_slice(&0u32.to_le_bytes());
        data.extend_from_slice(&0u32.to_le_bytes());
        data.extend_from_slice(b" LBL");
        data.extend_from_slice(&1u32.to_le_bytes()); // strings for this label
        data.extend_from_slice(&1000u32.to_le_bytes()); // id len
        data.extend_from_slice(b"ab");
        let parsed = parse_csf(&data).unwrap();
        assert_eq!(parsed.entries.len(), 0);
        assert!(parsed.truncated);
    }

    #[test]
    fn abi_sizing_call() {
        let data = build_csf(&[("Name:1".into(), "hello".into(), None)]);
        let mut entry_count = 0usize;
        let mut ids_len = 0usize;
        let mut values_len = 0usize;
        let mut values_asc_len = 0usize;
        let mut truncated = 0i32;
        let status = unsafe {
            rs_csf_parse(
                data.as_ptr(),
                data.len(),
                std::ptr::null_mut(),
                0,
                &mut entry_count,
                std::ptr::null_mut(),
                0,
                &mut ids_len,
                std::ptr::null_mut(),
                0,
                &mut values_len,
                std::ptr::null_mut(),
                0,
                &mut values_asc_len,
                &mut truncated,
            )
        };
        assert_eq!(status, RS_ERR_SMALL_BUFFER);
        assert_eq!(entry_count, 1);
        assert_eq!(ids_len, 6);
        assert_eq!(values_len, 10);
        assert_eq!(values_asc_len, 0);
        assert_eq!(truncated, 0);

        // second call with buffers succeeds
        let mut entries = vec![RsCsfEntry { id_len: 0, value_len: 0, value_asc_len: 0 }; 1];
        let mut ids = vec![0u8; ids_len];
        let mut values = vec![0u8; values_len];
        let status = unsafe {
            rs_csf_parse(
                data.as_ptr(),
                data.len(),
                entries.as_mut_ptr(),
                entries.len(),
                &mut entry_count,
                ids.as_mut_ptr(),
                ids.len(),
                &mut ids_len,
                values.as_mut_ptr(),
                values.len(),
                &mut values_len,
                std::ptr::null_mut(),
                0,
                &mut values_asc_len,
                &mut truncated,
            )
        };
        assert_eq!(status, RS_OK);
        assert_eq!(&ids[..], b"Name:1");
        let val: Vec<u16> = values
            .chunks_exact(2)
            .map(|c| u16::from_le_bytes([c[0], c[1]]))
            .collect();
        assert_eq!(String::from_utf16_lossy(&val), "hello");
    }
}
