//! Minimal safe wrapper around the vendored minilzo (LZO 2.10) compiled
//! with renamed symbols (see vendor/minilzo/minilzo_rs.c) so that it does
//! not collide with the lzo2.dll import library linked by the editor.
//!
//! Only the three functions the pack codecs need are exposed:
//!   * `lzo1x_1_compress`  - used by encode5s (IsoMapPack5 encoding)
//!   * `lzo1x_decompress_safe` - bounds-checked decompression used by
//!     decode5s (IsoMapPack5 decoding); the unchecked variant is not
//!     exposed at all.
//!
//! Note: minilzo's `lzo_uint` is asserted (at compile time, in
//! lzoconf.h) to match `size_t`, so lengths are declared as `usize`
//! here - this is ABI-correct on both x86 and x86_64.
//!
//! LZO is GPL v2+ (see vendor/minilzo/README.LZO).

use std::os::raw::{c_int, c_long, c_short, c_void};

/// wrkmem size for lzo1x_1_compress. minilzo needs
/// 16384 * sizeof(lzo_dict_t) (= 65536 on 32-bit builds); 128 KiB is
/// enough on every supported target.
pub(crate) const LZO1X_1_MEM_COMPRESS: usize = 131072;

const LZO_VERSION: u32 = 0x20a0; // LZO 2.10

/// Mirrors `lzo_callback_t` (three function pointers + two lzo_uint +
/// one void*), only needed for its size in `__lzo_init_v2`.
#[repr(C)]
struct LzoCallback {
    nalloc: usize,
    nfree: usize,
    nprogress: usize,
    user1: usize,
    user2: usize,
    user3: usize,
}

extern "C" {
    fn rs_core___lzo_init_v2(
        v: u32,
        s1: c_int,
        s2: c_int,
        s3: c_int,
        s4: c_int,
        s5: c_int,
        s6: c_int,
        s7: c_int,
        s8: c_int,
        s9: c_int,
    ) -> c_int;
    fn rs_core_lzo1x_1_compress(
        src: *const u8,
        src_len: usize,
        dst: *mut u8,
        dst_len: *mut usize,
        wrkmem: *mut c_void,
    ) -> c_int;
    fn rs_core_lzo1x_decompress_safe(
        src: *const u8,
        src_len: usize,
        dst: *mut u8,
        dst_len: *mut usize,
        wrkmem: *mut c_void,
    ) -> c_int;
}

/// Initializes the minilzo library (must be called once before any other
/// function).
pub(crate) fn lzo_init() -> Result<(), ()> {
    let code = unsafe {
        rs_core___lzo_init_v2(
            LZO_VERSION,
            std::mem::size_of::<c_short>() as c_int,
            std::mem::size_of::<c_int>() as c_int,
            std::mem::size_of::<c_long>() as c_int,
            std::mem::size_of::<u32>() as c_int,
            std::mem::size_of::<usize>() as c_int, // sizeof(lzo_uint) == size_t
            std::mem::size_of::<usize>() as c_int, // lzo_sizeof_dict_t == sizeof(lzo_bytep)
            std::mem::size_of::<usize>() as c_int, // sizeof(char *)
            std::mem::size_of::<usize>() as c_int, // sizeof(lzo_voidp)
            std::mem::size_of::<LzoCallback>() as c_int,
        )
    };
    if code == 0 {
        Ok(())
    } else {
        Err(())
    }
}

/// Compresses with lzo1x_1_compress into `dst` (which must have enough
/// room; callers preallocate generously). Returns the compressed length.
pub(crate) fn lzo_compress(
    src: &[u8],
    dst: &mut [u8],
    wrkmem: &mut [u8; LZO1X_1_MEM_COMPRESS],
) -> Result<usize, ()> {
    let mut dst_len = dst.len();
    let code = unsafe {
        rs_core_lzo1x_1_compress(
            src.as_ptr(),
            src.len(),
            dst.as_mut_ptr(),
            &mut dst_len,
            wrkmem.as_mut_ptr() as *mut c_void,
        )
    };
    if code != 0 {
        return Err(());
    }
    Ok(dst_len)
}

/// Decompresses with lzo1x_decompress_safe; `dst.len()` must be the
/// exact expected output size (known from the pack section header).
/// Returns the actual decompressed length.
pub(crate) fn lzo_decompress_safe(src: &[u8], dst: &mut [u8]) -> Result<usize, ()> {
    let mut dst_len = dst.len();
    let code = unsafe {
        rs_core_lzo1x_decompress_safe(
            src.as_ptr(),
            src.len(),
            dst.as_mut_ptr(),
            &mut dst_len,
            std::ptr::null_mut(),
        )
    };
    if code != 0 {
        return Err(());
    }
    Ok(dst_len)
}
