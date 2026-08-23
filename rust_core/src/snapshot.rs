//! Compact, bounds-checked storage for map undo snapshots.
//!
//! Map cells contain many repeated bytes (empty overlay values, common tile
//! attributes). A small PackBits-style codec keeps the 64-entry undo history
//! from retaining nine separately allocated full-size arrays. Decoding lives
//! here because malformed compressed history must never be able to overrun the
//! editor's destination buffer.

use crate::{RS_ERR_BAD_ARG, RS_ERR_PANIC, RS_ERR_SMALL_BUFFER, RS_OK};
use std::panic::{catch_unwind, AssertUnwindSafe};

fn encode(input: &[u8], output: &mut Vec<u8>) {
    let mut pos = 0;
    while pos < input.len() {
        let mut run = 1;
        while pos + run < input.len() && input[pos + run] == input[pos] && run < 130 {
            run += 1;
        }
        if run >= 3 {
            output.push(0x80 | ((run - 3) as u8));
            output.push(input[pos]);
            pos += run;
            continue;
        }

        let literal_start = pos;
        pos += run;
        while pos < input.len() && pos - literal_start < 128 {
            let mut next_run = 1;
            while pos + next_run < input.len()
                && input[pos + next_run] == input[pos]
                && next_run < 3
            {
                next_run += 1;
            }
            if next_run >= 3 {
                break;
            }
            pos += next_run.min(128 - (pos - literal_start));
        }
        output.push((pos - literal_start - 1) as u8);
        output.extend_from_slice(&input[literal_start..pos]);
    }
}

fn decode(input: &[u8], output: &mut [u8]) -> bool {
    let (mut src, mut dst) = (0, 0);
    while src < input.len() {
        let control = input[src];
        src += 1;
        if control & 0x80 != 0 {
            let len = (control as usize & 0x7f) + 3;
            if src >= input.len() || len > output.len().saturating_sub(dst) {
                return false;
            }
            output[dst..dst + len].fill(input[src]);
            src += 1;
            dst += len;
        } else {
            let len = control as usize + 1;
            if len > input.len().saturating_sub(src) || len > output.len().saturating_sub(dst) {
                return false;
            }
            output[dst..dst + len].copy_from_slice(&input[src..src + len]);
            src += len;
            dst += len;
        }
    }
    dst == output.len()
}

#[no_mangle]
pub unsafe extern "C" fn rs_snapshot_pack(
    input: *const u8,
    input_len: usize,
    output: *mut u8,
    output_cap: usize,
    output_len: *mut usize,
) -> i32 {
    match catch_unwind(AssertUnwindSafe(|| {
        if output_len.is_null() || (input_len != 0 && input.is_null()) {
            return RS_ERR_BAD_ARG;
        }
        let source = if input_len == 0 {
            &[]
        } else {
            std::slice::from_raw_parts(input, input_len)
        };
        let mut encoded = Vec::with_capacity(input_len / 2);
        encode(source, &mut encoded);
        *output_len = encoded.len();
        if encoded.len() > output_cap || (encoded.len() != 0 && output.is_null()) {
            return RS_ERR_SMALL_BUFFER;
        }
        if !encoded.is_empty() {
            std::ptr::copy_nonoverlapping(encoded.as_ptr(), output, encoded.len());
        }
        RS_OK
    })) {
        Ok(code) => code,
        Err(_) => RS_ERR_PANIC,
    }
}

#[no_mangle]
pub unsafe extern "C" fn rs_snapshot_unpack(
    input: *const u8,
    input_len: usize,
    output: *mut u8,
    output_len: usize,
) -> i32 {
    match catch_unwind(AssertUnwindSafe(|| {
        if (input_len != 0 && input.is_null()) || (output_len != 0 && output.is_null()) {
            return RS_ERR_BAD_ARG;
        }
        let source = if input_len == 0 {
            &[]
        } else {
            std::slice::from_raw_parts(input, input_len)
        };
        let destination = if output_len == 0 {
            &mut []
        } else {
            std::slice::from_raw_parts_mut(output, output_len)
        };
        if decode(source, destination) {
            RS_OK
        } else {
            RS_ERR_BAD_ARG
        }
    })) {
        Ok(code) => code,
        Err(_) => RS_ERR_PANIC,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn round_trips_runs_and_literals() {
        let source = [vec![0xaa; 300], (0..=255).collect(), vec![1, 1, 2, 2, 2, 3]].concat();
        let mut packed = Vec::new();
        encode(&source, &mut packed);
        let mut decoded = vec![0; source.len()];
        assert!(decode(&packed, &mut decoded));
        assert_eq!(decoded, source);
        assert!(packed.len() < source.len());
    }

    #[test]
    fn rejects_truncated_or_wrong_sized_data() {
        assert!(!decode(&[0x82], &mut [0; 5]));
        assert!(!decode(&[0, 7], &mut [0; 2]));
    }
}
