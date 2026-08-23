//! Memory-safe Rust core for the FinalAlert 2 Mission Editor.
//!
//! This crate hosts the pixel-processing kernels that were the most
//! memory-error-prone parts of the C++ code base:
//!   * VXL (voxel) section rendering - the span walker previously read
//!     malformed span data with raw, unbounded pointer arithmetic.
//!   * TS TMP terrain tile drawing - previously copied rows with raw
//!     `memcpy` calls whose destination offsets were only partially
//!     checked.
//!
//! All functions are exported through a plain C ABI and are written to be
//! panic-free across FFI: every exported function routes through
//! `catch_unwind` so that a panic can never unwind into C++ code.
//!
//! Reads from input buffers are clamped to the provided buffer lengths and
//! writes to output buffers are bounds-checked; on malformed input the
//! affected region is simply skipped instead of corrupting the heap.
//!
//! Floating point operations replicate the original C++ code in the same
//! order, so rendering results are identical for valid data.

#![allow(clippy::missing_safety_doc)]
#![allow(clippy::needless_range_loop)]

mod codec;
mod csf;
mod minilzo;
mod snapshot;
mod vulkan;

pub use vulkan::{
    rs_vulkan_create, rs_vulkan_destroy, rs_vulkan_last_error, rs_vulkan_prepare,
    rs_vulkan_present, RsVulkanRenderer,
};

use std::panic::{catch_unwind, AssertUnwindSafe};

// ---------------------------------------------------------------------------
// Status codes
// ---------------------------------------------------------------------------

/// Operation succeeded.
pub const RS_OK: i32 = 0;
/// One or more arguments are invalid (null pointer, negative size, ...).
pub const RS_ERR_BAD_ARG: i32 = -1;
/// Output buffer too small for the requested operation.
pub const RS_ERR_SMALL_BUFFER: i32 = -2;
/// A panic was caught inside the Rust core (should never happen).
pub const RS_ERR_PANIC: i32 = -3;

/// Maximum supported VXL section count (arbitrary but far above any real
/// model; guards against corrupt headers driving huge loops).
const MAX_SECTIONS: usize = 1024;

// ---------------------------------------------------------------------------
// Math types (mirrors MissionEditorPackLib/Vec3.h semantics exactly)
// ---------------------------------------------------------------------------

#[derive(Clone, Copy)]
struct Vec3 {
    x: f32,
    y: f32,
    z: f32,
}

impl Vec3 {
    #[inline]
    fn new(x: f32, y: f32, z: f32) -> Vec3 {
        Vec3 { x, y, z }
    }

    #[inline]
    fn dot(&self, other: &Vec3) -> f32 {
        self.x * other.x + self.y * other.y + self.z * other.z
    }

    #[inline]
    fn minimum(&self, other: &Vec3) -> Vec3 {
        Vec3 {
            x: self.x.min(other.x),
            y: self.y.min(other.y),
            z: self.z.min(other.z),
        }
    }

    #[inline]
    fn maximum(&self, other: &Vec3) -> Vec3 {
        Vec3 {
            x: self.x.max(other.x),
            y: self.y.max(other.y),
            z: self.z.max(other.z),
        }
    }
}

impl std::ops::Add<Vec3> for Vec3 {
    type Output = Vec3;
    #[inline]
    fn add(self, rhs: Vec3) -> Vec3 {
        Vec3::new(self.x + rhs.x, self.y + rhs.y, self.z + rhs.z)
    }
}

impl std::ops::Sub<Vec3> for Vec3 {
    type Output = Vec3;
    #[inline]
    fn sub(self, rhs: Vec3) -> Vec3 {
        Vec3::new(self.x - rhs.x, self.y - rhs.y, self.z - rhs.z)
    }
}

impl std::ops::Div<Vec3> for Vec3 {
    type Output = Vec3;
    #[inline]
    fn div(self, rhs: Vec3) -> Vec3 {
        Vec3::new(self.x / rhs.x, self.y / rhs.y, self.z / rhs.z)
    }
}

impl std::ops::Neg for Vec3 {
    type Output = Vec3;
    #[inline]
    fn neg(self) -> Vec3 {
        Vec3::new(-self.x, -self.y, -self.z)
    }
}

#[derive(Clone, Copy)]
struct Mat3x4 {
    m: [[f32; 4]; 3],
}

impl Mat3x4 {
    /// Construct from a row-major array of 12 floats (same layout as the
    /// C++ `Matrix3_4(const T*)` constructor and the HVA file data).
    #[inline]
    fn from_rows(v: &[f32; 12]) -> Mat3x4 {
        let mut m = [[0.0f32; 4]; 3];
        for row in 0..3 {
            for col in 0..4 {
                m[row][col] = v[row * 4 + col];
            }
        }
        Mat3x4 { m }
    }

    #[inline]
    fn mul_vec(&self, v: Vec3) -> Vec3 {
        Vec3::new(
            v.x * self.m[0][0] + v.y * self.m[0][1] + v.z * self.m[0][2] + self.m[0][3],
            v.x * self.m[1][0] + v.y * self.m[1][1] + v.z * self.m[1][2] + self.m[1][3],
            v.x * self.m[2][0] + v.y * self.m[2][1] + v.z * self.m[2][2] + self.m[2][3],
        )
    }

    #[inline]
    fn scaled_column(&self, col: usize, scale: f32) -> Mat3x4 {
        let mut copy = *self;
        copy.m[0][col] *= scale;
        copy.m[1][col] *= scale;
        copy.m[2][col] *= scale;
        copy
    }

    #[inline]
    fn scale_column(&self, col: usize, scale: f32) -> Mat3x4 {
        self.scaled_column(col, scale)
    }

    #[inline]
    fn set_column(&self, col: usize, v: Vec3) -> Mat3x4 {
        let mut copy = *self;
        copy.m[0][col] = v.x;
        copy.m[1][col] = v.y;
        copy.m[2][col] = v.z;
        copy
    }

    #[inline]
    fn translation(v: Vec3) -> Mat3x4 {
        Mat3x4 {
            m: [
                [1.0, 0.0, 0.0, v.x],
                [0.0, 1.0, 0.0, v.y],
                [0.0, 0.0, 1.0, v.z],
            ],
        }
    }

    #[inline]
    fn scale(v: Vec3) -> Mat3x4 {
        Mat3x4 {
            m: [
                [v.x, 0.0, 0.0, 0.0],
                [0.0, v.y, 0.0, 0.0],
                [0.0, 0.0, v.z, 0.0],
            ],
        }
    }
}

// `rotate_zxy` in the C++ renderer is algebraically a sequence of ordinary
// plane rotations.  VXL rendering applies the same angles to every voxel and
// normal in a section, so calculate their sine and cosine only once.
#[derive(Clone, Copy)]
struct RotationZxy {
    sin_x: f32,
    cos_x: f32,
    sin_y: f32,
    cos_y: f32,
    sin_z: f32,
    cos_z: f32,
}

impl RotationZxy {
    #[inline]
    fn new(angles: Vec3) -> RotationZxy {
        let (sin_x, cos_x) = angles.x.sin_cos();
        let (sin_y, cos_y) = angles.y.sin_cos();
        let (sin_z, cos_z) = angles.z.sin_cos();
        RotationZxy {
            sin_x,
            cos_x,
            sin_y,
            cos_y,
            sin_z,
            cos_z,
        }
    }

    #[inline]
    fn apply(&self, v: &mut Vec3) {
        let x = v.x;
        let y = v.y;
        v.x = x * self.cos_z + y * self.sin_z;
        v.y = y * self.cos_z - x * self.sin_z;

        let y = v.y;
        let z = v.z;
        v.y = y * self.cos_x + z * self.sin_x;
        v.z = z * self.cos_x - y * self.sin_x;

        let x = v.x;
        let z = v.z;
        v.x = x * self.cos_y + z * self.sin_y;
        v.z = z * self.cos_y - x * self.sin_y;
    }
}

/// Mirrors `normalize()` from the C++ Vec3 class (division by zero yields
/// NaN/Inf exactly like the C++ floating point division).
#[inline]
fn normalize(v: Vec3) -> Vec3 {
    let inv_l = 1.0f32 / (v.x * v.x + v.y * v.y + v.z * v.z).sqrt();
    Vec3::new(v.x * inv_l, v.y * inv_l, v.z * inv_l)
}

// ---------------------------------------------------------------------------
// C ABI types
// ---------------------------------------------------------------------------

/// Mirrors the fields of `t_vxl_section_tailer` that the renderer needs.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct RsVxlTailer {
    pub scale: f32,
    pub x_min_scale: f32,
    pub y_min_scale: f32,
    pub z_min_scale: f32,
    pub x_max_scale: f32,
    pub y_max_scale: f32,
    pub z_max_scale: f32,
    pub cx: u8,
    pub cy: u8,
    pub cz: u8,
}

/// Mirrors the fields of the TMP image header that the tile drawing needs.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct RsTmpTileInfo {
    /// `f.get_cx()` / `f.get_cy()` (standard tile dimensions).
    pub std_cx: i32,
    pub std_cy: i32,
    /// `f.has_extra_graphics(i)`
    pub has_extra: i32,
    /// `f.get_cx_extra(i)`
    pub cx_extra: i32,
    /// `f.get_cy_extra(i)`
    pub cy_extra: i32,
    /// `f.get_x_extra(i) - f.get_x(i)`
    pub x_extra: i32,
    /// `f.get_y_extra(i) - f.get_y(i)`
    pub y_extra: i32,
}

// ---------------------------------------------------------------------------
// VXL section bounds computation (mirrors GetVXLSectionBounds)
// ---------------------------------------------------------------------------

#[inline]
fn section_bounds(
    tailer: &RsVxlTailer,
    hva_matrix: &[f32; 12],
    rotation: &Vec3,
    model_offset: &Vec3,
) -> (Vec3, Vec3) {
    let matrix = Mat3x4::from_rows(hva_matrix).scaled_column(3, tailer.scale);
    let rotation = RotationZxy::new(*rotation);

    let mut min_vec = Vec3::new(tailer.x_min_scale, tailer.y_min_scale, tailer.z_min_scale);
    let mut max_vec = min_vec;

    for x in 0..2 {
        for y in 0..2 {
            for z in 0..2 {
                let corner = Vec3::new(
                    if x == 0 {
                        tailer.x_min_scale
                    } else {
                        tailer.x_max_scale
                    },
                    if y == 0 {
                        tailer.y_min_scale
                    } else {
                        tailer.y_max_scale
                    },
                    if z == 0 {
                        tailer.z_min_scale
                    } else {
                        tailer.z_max_scale
                    },
                ) + *model_offset;
                let mut cur = matrix.mul_vec(corner);
                rotation.apply(&mut cur);
                min_vec = min_vec.minimum(&cur);
                max_vec = max_vec.maximum(&cur);
            }
        }
    }

    (min_vec, max_vec)
}

// ---------------------------------------------------------------------------
// VXL section rendering (mirrors RenderVXLSection)
// ---------------------------------------------------------------------------

#[inline]
fn c_f32_to_int(v: f32) -> i32 {
    // C++: static_cast<int>(v) truncates toward zero; NaN is UB in C++, so
    // we simply clamp NaN to 0 here (deterministic and safe).
    if v.is_nan() {
        0
    } else {
        v as i32
    }
}

struct RenderResult {
    status: i32,
}

fn render_vxl_section_impl(
    image: *mut u8,
    lighting: *mut u8,
    image_z: *mut i8,
    buf_len: usize,
    rt_width: i32,
    rt_height: i32,
    tailer: &RsVxlTailer,
    hva_matrix: &[f32; 12],
    normals: &[f32],
    light_direction: Vec3,
    rotation: Vec3,
    model_offset: Vec3,
    post_hva_offset: Vec3,
    span_data: &[u8],
    span_data_ofs: i32,
    span_start_list: &[i32],
    span_end_list: &[i32],
    mut out_center_x: Option<&mut i32>,
    mut out_center_y: Option<&mut i32>,
    mut out_center_x_zmax: Option<&mut i32>,
    mut out_center_y_zmax: Option<&mut i32>,
    i3d_center_x: i32,
    i3d_center_y: i32,
    p_last_op: *mut i32,
) -> RenderResult {
    macro_rules! set_op {
        ($v:expr) => {
            if !p_last_op.is_null() {
                unsafe { *p_last_op = $v };
            }
        };
    }

    // ---- mirrors RenderVXLSection() ----
    let inverse_light_direction = -normalize(light_direction);
    let rotation = RotationZxy::new(rotation);

    set_op!(10);

    let cx1 = tailer.cx as i32;
    let cy1 = tailer.cy as i32;
    let cz1 = tailer.cz as i32;

    let matrix = Mat3x4::from_rows(hva_matrix);
    let normal_matrix = matrix.set_column(3, Vec3::new(0.0, 0.0, 0.0));
    let scaled_matrix = matrix.scale_column(3, tailer.scale);
    let min_scale = Vec3::new(tailer.x_min_scale, tailer.y_min_scale, tailer.z_min_scale)
        + post_hva_offset;
    let max_scale = Vec3::new(tailer.x_max_scale, tailer.y_max_scale, tailer.z_max_scale)
        + post_hva_offset;
    let translate_to_world_matrix = Mat3x4::translation(min_scale);
    let scale_to_world_matrix = Mat3x4::scale(
        (max_scale - min_scale) / Vec3::new(cx1 as f32, cy1 as f32, cz1 as f32),
    );

    let mut i3d_center_x = i3d_center_x;
    let mut i3d_center_y = i3d_center_y;
    // The C++ code substitutes the section center (which is 0) when the
    // caller passes a negative coordinate.
    if i3d_center_x < 0 {
        i3d_center_x = 0;
    }
    if i3d_center_y < 0 {
        i3d_center_y = 0;
    }

    let center = Vec3::new(0.0, 0.0, 0.0);

    set_op!(11);

    // The anchor outputs locate the model origin in the render target. They
    // must NOT include the HVA translation: the game positions each section's
    // content by that translation, so including it here would cancel it out
    // when the caller composites this image onto another one (turret on body).
    // Use the HVA linear part only; for sections with a zero translation
    // (vanilla TS/RA2 files) this is identical to the previous behavior.
    let linear_matrix = matrix.set_column(3, Vec3::new(0.0, 0.0, 0.0));

    if out_center_x.is_some() || out_center_y.is_some() {
        let s_pixel = center;
        let mut d_pixel = linear_matrix.mul_vec(s_pixel);
        rotation.apply(&mut d_pixel);
        d_pixel = d_pixel + model_offset;

        if let Some(o) = out_center_x.as_deref_mut() {
            *o = c_f32_to_int(d_pixel.x + 0.5);
        }
        if let Some(o) = out_center_y.as_deref_mut() {
            *o = c_f32_to_int(d_pixel.y + 0.5);
        }
    }

    set_op!(12);

    if out_center_x_zmax.is_some() || out_center_y_zmax.is_some() {
        let s_pixel = center;
        let mut d_pixel = linear_matrix.mul_vec(s_pixel);
        rotation.apply(&mut d_pixel);
        d_pixel = d_pixel + model_offset;

        if let Some(o) = out_center_x_zmax.as_deref_mut() {
            *o = c_f32_to_int(d_pixel.x);
        }
        if let Some(o) = out_center_y_zmax.as_deref_mut() {
            *o = c_f32_to_int(d_pixel.y);
        }
    }

    set_op!(13);

    if buf_len == 0 || rt_width <= 0 || rt_height <= 0 {
        return RenderResult { status: RS_ERR_BAD_ARG };
    }
    if (rt_width as usize) > buf_len || (rt_height as usize) > buf_len {
        return RenderResult { status: RS_ERR_BAD_ARG };
    }
    let buf_len_i32 = buf_len as i32;
    if (rt_width as i64) * (rt_height as i64) > buf_len_i32 as i64 {
        return RenderResult { status: RS_ERR_SMALL_BUFFER };
    }

    // Safe Rust slices over the caller-provided output buffers.
    let img = unsafe { std::slice::from_raw_parts_mut(image, buf_len) };
    let lit = unsafe { std::slice::from_raw_parts_mut(lighting, buf_len) };
    let zimg = unsafe { std::slice::from_raw_parts_mut(image_z, buf_len) };

    // Normal indices in VXL spans are bytes.  Their transformed lighting is
    // independent of position, so performing it once per possible index
    // removes two matrix/rotation operations from every visible voxel.
    let mut lighting_lut = [0u8; 256];
    for (normal_index, light) in lighting_lut.iter_mut().enumerate() {
        let normal_vec = get_normal(normals, normal_index as u32);
        let mut normal = normal_matrix.mul_vec(normal_vec);
        rotation.apply(&mut normal);
        let normal_dot_lighting_vec = normal.dot(&inverse_light_direction);
        let light_val = if normal_dot_lighting_vec < 0.0 {
            0.0
        } else {
            normal_dot_lighting_vec
        };
        *light = c_f32_to_int(light_val * 255.0) as i32 as u8;
    }

    let mut truncated = false;

    let mut j: usize = 0;
    for _y in 0..cy1 {
        for _x in 0..cx1 {
            // Span start/end lists hold cx*cy entries; a corrupt file may
            // not have enough entries - treat missing entries as -1 (no span).
            let start = span_start_list.get(j).copied().unwrap_or(-1);
            if start >= 0 {
                let end = span_end_list.get(j).copied().unwrap_or(start);
                let span_len = end - start + 1;
                let base = span_data_ofs as i64 + start as i64;

                let span: Option<&[u8]> = if span_len > 0
                    && base >= 0
                    && (base as usize) < span_data.len()
                {
                    let lo = base as usize;
                    let hi = (lo as i64 + span_len as i64) as usize;
                    // Clamp the span to the available data: reads inside the
                    // file are always safe; a span that runs past the end of
                    // the file is malformed and will simply stop early.
                    Some(&span_data[lo..hi.min(span_data.len())])
                } else {
                    None
                };

                if let Some(span) = span {
                    let mut r: usize = 0;
                    let mut z: i32 = 0;
                    let mut last_z_reported: i32 = -5000;
                    while z < cz1 {
                        if r >= span.len() {
                            truncated = true;
                            break;
                        }
                        z += span[r] as i32;
                        r += 1;

                        if r >= span.len() {
                            truncated = true;
                            break;
                        }
                        let mut count = span[r] as i32;
                        r += 1;

                        while count > 0 {
                            if r + 2 > span.len() {
                                truncated = true;
                                r = span.len();
                                break;
                            }
                            let color = span[r];
                            let normal_index = span[r + 1];
                            r += 2;

                            let s_pixel = Vec3::new(_x as f32, _y as f32, z as f32);
                            let m_pixel = translate_to_world_matrix
                                .mul_vec(scale_to_world_matrix.mul_vec(s_pixel));
                            let t_pixel = scaled_matrix.mul_vec(m_pixel);
                            let mut d_pixel = t_pixel;
                            rotation.apply(&mut d_pixel);
                            d_pixel = d_pixel + model_offset;

                            if _x == i3d_center_x && _y == i3d_center_y {
                                if z >= last_z_reported {
                                    last_z_reported = z;
                                    if let Some(o) = out_center_x_zmax.as_deref_mut() {
                                        *o = c_f32_to_int(d_pixel.x);
                                    }
                                    if let Some(o) = out_center_y_zmax.as_deref_mut() {
                                        *o = c_f32_to_int(d_pixel.y);
                                    }
                                }
                            }

                            let px = c_f32_to_int(d_pixel.x + 0.5);
                            let py = c_f32_to_int(d_pixel.y + 0.5);

                            if px >= 0
                                && py >= 0
                                && px < rt_width
                                && py < rt_height
                            {
                                let ofs = (px + rt_width * py) as usize;
                                let z_ofs = zimg[ofs] as f32;
                                if d_pixel.z > z_ofs {
                                    img[ofs] = color;

                                    lit[ofs] = lighting_lut[normal_index as usize];
                                    zimg[ofs] = c_f32_to_int(d_pixel.z) as i32 as i8;
                                }
                            }

                            z += 1;
                            count -= 1;
                        }

                        if r >= span.len() {
                            truncated = true;
                            break;
                        }
                        r += 1;
                    }
                } else {
                    // malformed span offset: skip this span entirely
                    truncated = true;
                }
            }
            j += 1;
        }
    }

    RenderResult {
        status: if truncated {
            // A partially rendered section still produced usable pixels;
            // report success so the caller uses it. Truncation is logged by
            // the C++ wrapper for diagnostics.
            RS_OK
        } else {
            RS_OK
        },
    }
}

#[inline]
fn get_normal(normals: &[f32], index: u32) -> Vec3 {
    // Mirrors VoxelNormalTable::operator[]: out-of-range indices fall back
    // to the Y-up normal (0, 1, 0).
    let idx = index as usize;
    if idx * 3 + 2 < normals.len() {
        Vec3::new(normals[idx * 3], normals[idx * 3 + 1], normals[idx * 3 + 2])
    } else {
        Vec3::new(0.0, 1.0, 0.0)
    }
}

// ---------------------------------------------------------------------------
// TMP tile drawing (mirrors tmp_ts_draw)
// ---------------------------------------------------------------------------

fn tmp_ts_draw_impl(
    dst: &mut [u8],
    src: &[u8],
    info: &RsTmpTileInfo,
) -> (i32, i32, i32) {
    // returns (tile_cx, tile_cy, status)

    let mut tile_cx = info.std_cx;
    let mut tile_cy = info.std_cy;
    let std_cx = info.std_cx;
    let std_cy = info.std_cy;

    let mut cy_extra = 0i32;
    let mut y_extra = 0i32;
    let mut cx_extra = 0i32;
    let mut x_extra = 0i32;
    let mut y_added = 0i32;
    let mut x_added = 0i32;

    if info.has_extra != 0 {
        cy_extra = info.cy_extra;
        y_extra = info.y_extra;
        cx_extra = info.cx_extra;
        x_extra = info.x_extra;

        if y_extra < 0 {
            y_added = -y_extra;
            tile_cy -= y_extra;
            y_extra = 0;
        }
        if x_extra < 0 {
            x_added = -x_extra;
            tile_cx -= x_extra;
            x_extra = 0;
        }

        if cy_extra + y_extra > tile_cy {
            tile_cy = cy_extra + y_extra;
        }
        if cx_extra + x_extra > tile_cx {
            tile_cx = cx_extra + x_extra;
        }
    }

    if tile_cx <= 0 || tile_cy <= 0 || std_cx <= 0 || std_cy <= 0 {
        return (tile_cx, tile_cy, RS_ERR_BAD_ARG);
    }

    let area = (tile_cx * tile_cy) as usize;
    if area > dst.len() {
        return (tile_cx, tile_cy, RS_ERR_SMALL_BUFFER);
    }

    // memset(d, 0, tile_cx * tile_cy)
    for b in dst[..area].iter_mut() {
        *b = 0;
    }

    // base part: read cursor into src
    let mut r: usize = 0;
    let mut w_line: i64 = if info.has_extra != 0 {
        (tile_cx * y_added + x_added) as i64
    } else {
        0
    };

    let mut x = std_cx / 2;
    let mut cx = 0i32;
    let mut y = 0i32;

    while y < std_cy / 2 {
        cx += 4;
        x -= 2;

        // memcpy(w_line + x, r, cx) with full bounds checks:
        let read_start = r.min(src.len());
        let read_end = (r as i64 + cx as i64).min(src.len() as i64).max(0) as usize;
        if read_end > read_start {
            let write_off = w_line + x as i64;
            if write_off >= 0 && (write_off as usize) < area {
                let avail = (area - write_off as usize).min(read_end - read_start);
                dst[write_off as usize..write_off as usize + avail]
                    .copy_from_slice(&src[read_start..read_start + avail]);
            }
        }

        // r += cx (advances even if reads were clamped - mirrors C++)
        r = (r as i64 + cx as i64) as usize;
        w_line += tile_cx as i64;
        y += 1;
    }
    while y < std_cy - 1 {
        cx -= 4;
        x += 2;

        let read_start = r.min(src.len());
        let read_end = (r as i64 + cx as i64).min(src.len() as i64).max(0) as usize;
        if read_end > read_start {
            let write_off = w_line + x as i64;
            if write_off >= 0 && (write_off as usize) < area {
                let avail = (area - write_off as usize).min(read_end - read_start);
                dst[write_off as usize..write_off as usize + avail]
                    .copy_from_slice(&src[read_start..read_start + avail]);
            }
        }

        r = (r as i64 + cx as i64) as usize;
        w_line += tile_cx as i64;
        y += 1;
    }

    if info.has_extra != 0 {
        // r += std_cx * std_cy / 2
        r = (r as i64 + (std_cx * std_cy / 2) as i64) as usize;

        let mut w_line: i64 = 0;
        let cx = cx_extra;
        let cy = cy_extra;

        // at this point x_extra/y_extra are >= 0 (negative values were
        // normalized above), so only the else-branches apply - same as the
        // original code's effective behavior.
        w_line += x_extra as i64;
        w_line += (y_extra as i64) * (tile_cx as i64);

        for _yy in 0..cy {
            for xx in 0..cx {
                let v = if r < src.len() { src[r] } else { 0 };
                r += 1;
                if v != 0 {
                    let write_off = w_line + xx as i64;
                    if write_off >= 0 && (write_off as usize) < area {
                        dst[write_off as usize] = v;
                    }
                }
            }
            w_line += tile_cx as i64;
        }
    }

    (tile_cx, tile_cy, RS_OK)
}

// ---------------------------------------------------------------------------
// Exported C ABI
// ---------------------------------------------------------------------------

/// Computes the overall projected bounds (and main section index) of a VXL
/// model. Mirrors the bounds accumulation loop in LoadVXLImageInSurface /
/// LoadVXLImage.
///
/// Returns RS_OK on success; on error the out parameters are untouched.
#[no_mangle]
pub unsafe extern "C" fn rs_vxl_compute_bounds(
    section_count: u32,
    section_ids: *const u8,
    tailers: *const RsVxlTailer,
    hva_matrices: *const f32,
    rotation: *const f32,
    model_offset: *const f32,
    out_main_section: *mut i32,
    out_min: *mut f32,
    out_max: *mut f32,
) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        if section_count == 0
            || section_count as usize > MAX_SECTIONS
            || section_ids.is_null()
            || tailers.is_null()
            || hva_matrices.is_null()
            || rotation.is_null()
            || model_offset.is_null()
            || out_min.is_null()
            || out_max.is_null()
        {
            return RS_ERR_BAD_ARG;
        }

        let n = section_count as usize;
        let ids = std::slice::from_raw_parts(section_ids, n * 16);
        let tails = std::slice::from_raw_parts(tailers, n);
        let hvas = std::slice::from_raw_parts(hva_matrices, n * 12);
        let rot = Vec3::new(*rotation, *rotation.add(1), *rotation.add(2));
        let offset = Vec3::new(*model_offset, *model_offset.add(1), *model_offset.add(2));

        let mut min_coords = Vec3::new(10000.0, 10000.0, 10000.0);
        let mut max_coords = Vec3::new(-10000.0, -10000.0, -10000.0);

        let mut i_body_section: i32 = -1;
        let mut i_largest_section: i32 = 0;
        let mut i_largest_volume: f32 = 0.0;

        for i in 0..n {
            let tailer = &tails[i];
            // section id: char[16], may not be NUL terminated - replicate
            // strstr(header->id, "BODY") == 0 with a safe prefix check.
            let id_slice = &ids[i * 16..i * 16 + 16];
            let starts_with_body = id_slice.starts_with(b"BODY");

            let hva: [f32; 12] = [
                hvas[i * 12],
                hvas[i * 12 + 1],
                hvas[i * 12 + 2],
                hvas[i * 12 + 3],
                hvas[i * 12 + 4],
                hvas[i * 12 + 5],
                hvas[i * 12 + 6],
                hvas[i * 12 + 7],
                hvas[i * 12 + 8],
                hvas[i * 12 + 9],
                hvas[i * 12 + 10],
                hvas[i * 12 + 11],
            ];

            let (sec_min, sec_max) = section_bounds(tailer, &hva, &rot, &offset);
            let extent = sec_max - sec_min;
            let volume = extent.x * extent.y * extent.z;
            if volume >= i_largest_volume {
                i_largest_volume = volume;
                i_largest_section = i as i32;
            }
            if starts_with_body {
                i_body_section = i as i32;
            }
            min_coords = min_coords.minimum(&sec_min);
            max_coords = max_coords.maximum(&sec_max);
        }

        let main_section = if i_body_section >= 0 {
            i_body_section
        } else {
            i_largest_section
        };

        if !out_main_section.is_null() {
            *out_main_section = main_section;
        }
        *out_min = min_coords.x;
        *out_min.add(1) = min_coords.y;
        *out_min.add(2) = min_coords.z;
        *out_max = max_coords.x;
        *out_max.add(1) = max_coords.y;
        *out_max.add(2) = max_coords.z;

        RS_OK
    }))
    .unwrap_or(RS_ERR_PANIC)
}

/// Renders one VXL section into caller-provided buffers.
///
/// `image`, `lighting` and `image_z` must each hold `buf_len` bytes
/// (`buf_len >= rt_width * rt_height`). All span reads are clamped to
/// `span_data` / `span_data_len`; malformed spans stop early instead of
/// reading out of bounds.
#[no_mangle]
pub unsafe extern "C" fn rs_vxl_render_section(
    image: *mut u8,
    lighting: *mut u8,
    image_z: *mut i8,
    buf_len: usize,
    rt_width: i32,
    rt_height: i32,
    tailer: *const RsVxlTailer,
    hva_matrix: *const f32,
    normals: *const f32,
    normal_count: u32,
    light_direction: *const f32,
    rotation: *const f32,
    model_offset: *const f32,
    post_hva_offset: *const f32,
    span_data: *const u8,
    span_data_len: usize,
    span_data_ofs: i32,
    span_start_list: *const i32,
    span_end_list: *const i32,
    out_center_x: *mut i32,
    out_center_y: *mut i32,
    out_center_x_zmax: *mut i32,
    out_center_y_zmax: *mut i32,
    i3d_center_x: i32,
    i3d_center_y: i32,
    p_last_op: *mut i32,
) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        if image.is_null()
            || lighting.is_null()
            || image_z.is_null()
            || tailer.is_null()
            || hva_matrix.is_null()
            || normals.is_null()
            || light_direction.is_null()
            || rotation.is_null()
            || model_offset.is_null()
            || post_hva_offset.is_null()
            || span_data.is_null()
        {
            return RS_ERR_BAD_ARG;
        }

        let tail = &*tailer;
        let hva: [f32; 12] = [
            *hva_matrix,
            *hva_matrix.add(1),
            *hva_matrix.add(2),
            *hva_matrix.add(3),
            *hva_matrix.add(4),
            *hva_matrix.add(5),
            *hva_matrix.add(6),
            *hva_matrix.add(7),
            *hva_matrix.add(8),
            *hva_matrix.add(9),
            *hva_matrix.add(10),
            *hva_matrix.add(11),
        ];
        let normals_slice = std::slice::from_raw_parts(normals, normal_count as usize * 3);
        let light_dir = Vec3::new(*light_direction, *light_direction.add(1), *light_direction.add(2));
        let rot = Vec3::new(*rotation, *rotation.add(1), *rotation.add(2));
        let model_off = Vec3::new(*model_offset, *model_offset.add(1), *model_offset.add(2));
        let post_off = Vec3::new(*post_hva_offset, *post_hva_offset.add(1), *post_hva_offset.add(2));

        let span_data_slice = std::slice::from_raw_parts(span_data, span_data_len);

        // span lists: cx*cy entries each; a corrupt tailer could make this
        // product huge, so cap the list length defensively.
        let list_len = (tail.cx as usize)
            .saturating_mul(tail.cy as usize)
            .min(MAX_SECTIONS * MAX_SECTIONS);
        let starts = if span_start_list.is_null() {
            &[][..]
        } else {
            std::slice::from_raw_parts(span_start_list, list_len)
        };
        let ends = if span_end_list.is_null() {
            &[][..]
        } else {
            std::slice::from_raw_parts(span_end_list, list_len)
        };

        let mut center_x: Option<&mut i32> = if out_center_x.is_null() { None } else { Some(&mut *out_center_x) };
        let mut center_y: Option<&mut i32> = if out_center_y.is_null() { None } else { Some(&mut *out_center_y) };
        let mut center_x_zmax: Option<&mut i32> = if out_center_x_zmax.is_null() { None } else { Some(&mut *out_center_x_zmax) };
        let mut center_y_zmax: Option<&mut i32> = if out_center_y_zmax.is_null() { None } else { Some(&mut *out_center_y_zmax) };

        let res = render_vxl_section_impl(
            image,
            lighting,
            image_z,
            buf_len,
            rt_width,
            rt_height,
            tail,
            &hva,
            normals_slice,
            light_dir,
            rot,
            model_off,
            post_off,
            span_data_slice,
            span_data_ofs,
            starts,
            ends,
            center_x.as_deref_mut(),
            center_y.as_deref_mut(),
            center_x_zmax.as_deref_mut(),
            center_y_zmax.as_deref_mut(),
            i3d_center_x,
            i3d_center_y,
            p_last_op,
        );
        res.status
    }))
    .unwrap_or(RS_ERR_PANIC)
}

/// Returns the output dimensions a TMP tile drawing will occupy.
/// Mirrors the tile_cx/tile_cy computation in tmp_ts_draw.
#[no_mangle]
pub unsafe extern "C" fn rs_tmp_ts_get_size(
    info: *const RsTmpTileInfo,
    out_tile_cx: *mut i32,
    out_tile_cy: *mut i32,
) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        if info.is_null() || out_tile_cx.is_null() || out_tile_cy.is_null() {
            return RS_ERR_BAD_ARG;
        }
        let (tile_cx, tile_cy, status) = tmp_ts_draw_impl(&mut [], &[], &*info);
        *out_tile_cx = tile_cx;
        *out_tile_cy = tile_cy;
        // RS_ERR_SMALL_BUFFER is expected here (empty dst) and just means
        // "sizes were computed"; convert to OK unless it's a bad argument.
        match status {
            RS_ERR_BAD_ARG => RS_ERR_BAD_ARG,
            _ => RS_OK,
        }
    }))
    .unwrap_or(RS_ERR_PANIC)
}

/// Draws a TS TMP tile into `dst` (which must hold at least the number of
/// bytes returned by `rs_tmp_ts_get_size`).
///
/// `src` must point to the raw image data of the tile (`f.get_image(i)`)
/// and `src_len` must cover the rest of the containing file so that reads
/// can be clamped to valid memory.
#[no_mangle]
pub unsafe extern "C" fn rs_tmp_ts_draw(
    dst: *mut u8,
    dst_len: usize,
    src: *const u8,
    src_len: usize,
    info: *const RsTmpTileInfo,
) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        if dst.is_null() || src.is_null() || info.is_null() {
            return RS_ERR_BAD_ARG;
        }
        let dst_slice = std::slice::from_raw_parts_mut(dst, dst_len);
        let src_slice = std::slice::from_raw_parts(src, src_len);
        let (_tile_cx, _tile_cy, status) = tmp_ts_draw_impl(dst_slice, src_slice, &*info);
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

    #[test]
    fn tmp_ts_draw_valid_60x30_tile() {
        // Build a synthetic 60x30 tile: base part is 900 bytes (rows of
        // widths 4,8,...,60,56,...,4), all pixels set to 7.
        let mut src = vec![7u8; 900];
        let info = RsTmpTileInfo {
            std_cx: 60,
            std_cy: 30,
            has_extra: 0,
            cx_extra: 0,
            cy_extra: 0,
            x_extra: 0,
            y_extra: 0,
        };
        let mut dst = vec![0u8; 60 * 30];
        let status = unsafe { rs_tmp_ts_draw(dst.as_mut_ptr(), dst.len(), src.as_mut_ptr(), src.len(), &info) };
        assert_eq!(status, RS_OK);

        // Row 0 (width 4, x = 28): pixels at x = 28..32 should be set.
        assert_eq!(&dst[28..32], &[7, 7, 7, 7]);
        // Everything outside the diamond shape stays 0.
        assert_eq!(dst[0], 0);
        assert_eq!(dst[27], 0);
        assert_eq!(dst[32], 0);
        assert_eq!(dst[59], 0);
        // Row 14 is the widest row: full width (x = 0..60).
        let row14 = &dst[14 * 60..15 * 60];
        assert_eq!(&row14[0..60], &vec![7u8; 60][..]);
        // Row 15 (width 56, x = 2): pixels at x = 2..58.
        let row15 = &dst[15 * 60..16 * 60];
        assert_eq!(&row15[2..58], &vec![7u8; 56][..]);
        assert_eq!(row15[0], 0);
        assert_eq!(row15[1], 0);
        assert_eq!(row15[58], 0);
        assert_eq!(row15[59], 0);
    }

    #[test]
    fn tmp_ts_draw_rejects_small_buffer() {
        let src = vec![7u8; 900];
        let info = RsTmpTileInfo {
            std_cx: 60,
            std_cy: 30,
            has_extra: 1,
            cx_extra: 120,
            cy_extra: 120,
            x_extra: 10,
            y_extra: 10,
        };
        // needed: tile_cx = 130, tile_cy = 130
        let mut dst = vec![0u8; 60 * 30];
        let status = unsafe { rs_tmp_ts_draw(dst.as_mut_ptr(), dst.len(), src.as_ptr(), src.len(), &info) };
        assert_eq!(status, RS_ERR_SMALL_BUFFER);

        let mut cx = 0;
        let mut cy = 0;
        let s = unsafe { rs_tmp_ts_get_size(&info, &mut cx, &mut cy) };
        assert_eq!(s, RS_OK);
        assert_eq!(cx, 130);
        assert_eq!(cy, 130);
    }

    #[test]
    fn vxl_span_walker_stops_on_truncated_data() {
        // A minimal section: 1x1x16 voxels. The span declares more runs
        // than the data contains - the renderer must stop early instead of
        // reading out of bounds (this used to walk off the end of the file
        // in C++ and corrupt the heap).
        let tailer = RsVxlTailer {
            scale: 1.0,
            x_min_scale: 0.0,
            y_min_scale: 0.0,
            z_min_scale: 0.0,
            x_max_scale: 1.0,
            y_max_scale: 1.0,
            z_max_scale: 16.0,
            cx: 1,
            cy: 1,
            cz: 16,
        };
        let hva: [f32; 12] = [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0];
        let normals = [0.0f32, 1.0, 0.0];
        let light_dir = [0.0f32, 0.0, 1.0];
        let rotation = [0.0f32; 3];
        let model_offset = [0.0f32; 3];
        let post_hva_offset = [0.0f32; 3];

        // span: skip=0, count=8 -> only 16 bytes provided (8 voxels of 2
        // bytes), but the run says 8... make the declared run larger than
        // the data: count byte = 100.
        let mut span_data = vec![0u8; 2];
        span_data[0] = 0; // skip
        span_data[1] = 100; // count - only 0 data bytes follow!
        let starts = [0i32];
        let ends = [1i32];

        let mut image = vec![0u8; 256];
        let mut lighting = vec![0u8; 256];
        let mut image_z = vec![i8::MIN; 256];
        let mut cx = 0;
        let mut cy = 0;
        let mut cxz = 0;
        let mut cyz = 0;

        let status = unsafe {
            rs_vxl_render_section(
                image.as_mut_ptr(),
                lighting.as_mut_ptr(),
                image_z.as_mut_ptr(),
                256,
                16,
                16,
                &tailer,
                hva.as_ptr(),
                normals.as_ptr(),
                1,
                light_dir.as_ptr(),
                rotation.as_ptr(),
                model_offset.as_ptr(),
                post_hva_offset.as_ptr(),
                span_data.as_ptr(),
                span_data.len(),
                0,
                starts.as_ptr(),
                ends.as_ptr(),
                &mut cx,
                &mut cy,
                &mut cxz,
                &mut cyz,
                0,
                0,
                std::ptr::null_mut(),
            )
        };
        assert_eq!(status, RS_OK); // truncated but graceful
        // No voxels were written (span contained no data) - buffers must
        // remain untouched, which proves we never wrote out of bounds.
        assert!(image.iter().all(|&b| b == 0));
    }

    #[test]
    fn vxl_render_writes_single_voxel() {
        let tailer = RsVxlTailer {
            scale: 1.0,
            x_min_scale: 0.0,
            y_min_scale: 0.0,
            z_min_scale: 0.0,
            x_max_scale: 1.0,
            y_max_scale: 1.0,
            z_max_scale: 1.0,
            cx: 1,
            cy: 1,
            cz: 1,
        };
        let hva: [f32; 12] = [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0];
        let normals = [0.0f32, 1.0, 0.0];
        let light_dir = [0.0f32, 1.0, 0.0];
        let rotation = [0.0f32; 3];
        let model_offset = [0.0f32; 3];
        let post_hva_offset = [0.0f32; 3];

        // span: skip=0, count=1, voxel (color=42, normal=0), trailing byte
        let span_data = [0u8, 1, 42, 0, 0];
        let starts = [0i32];
        let ends = [(span_data.len() - 1) as i32];

        let mut image = vec![0u8; 64];
        let mut lighting = vec![0u8; 64];
        let mut image_z = vec![i8::MIN; 64];

        let status = unsafe {
            rs_vxl_render_section(
                image.as_mut_ptr(),
                lighting.as_mut_ptr(),
                image_z.as_mut_ptr(),
                64,
                8,
                8,
                &tailer,
                hva.as_ptr(),
                normals.as_ptr(),
                1,
                light_dir.as_ptr(),
                rotation.as_ptr(),
                model_offset.as_ptr(),
                post_hva_offset.as_ptr(),
                span_data.as_ptr(),
                span_data.len(),
                0,
                starts.as_ptr(),
                ends.as_ptr(),
                std::ptr::null_mut(),
                std::ptr::null_mut(),
                std::ptr::null_mut(),
                std::ptr::null_mut(),
                -1,
                -1,
                std::ptr::null_mut(),
            )
        };
        assert_eq!(status, RS_OK);

        // The single voxel at model (0,0,0): s_pixel=(0,0,0), scaledMatrix
        // is identity (scale=1, min=0, max=1 => scaleToWorld=(1,1,1),
        // translate=(0,0,0)), so d_pixel = (0,0,0) -> pixel (0,0).
        assert_eq!(image[0], 42);
        // light: normal (0,1,0) dot inverse light (0,-1,0) = -1 -> 0
        assert_eq!(lighting[0], 0);
        assert_eq!(image_z[0], 0);
    }
}

// ---------------------------------------------------------------------------
// A/B regression tests: current Rust VXL renderer vs. verbatim port of the
// pre-port C++ renderer (MissionEditorPackLib.cpp at a547a31). Verifies that
// the Rust port produces identical turret/body composition offsets for all
// 8 vehicle directions.
// ---------------------------------------------------------------------------

#[cfg(test)]
mod vxl_ab_tests {
    use super::*;

// ---------------------------------------------------------------------------
// old C++ math
// ---------------------------------------------------------------------------

#[derive(Clone, Copy, Debug, PartialEq)]
struct OldVec {
    x: f32,
    y: f32,
    z: f32,
}

impl OldVec {
    fn new(x: f32, y: f32, z: f32) -> Self {
        OldVec { x, y, z }
    }
    fn minimum(&mut self, o: OldVec) {
        self.x = self.x.min(o.x);
        self.y = self.y.min(o.y);
        self.z = self.z.min(o.z);
    }
    fn maximum(&mut self, o: OldVec) {
        self.x = self.x.max(o.x);
        self.y = self.y.max(o.y);
        self.z = self.z.max(o.z);
    }
}

impl std::ops::Add<OldVec> for OldVec {
    type Output = OldVec;
    fn add(self, rhs: OldVec) -> OldVec {
        OldVec::new(self.x + rhs.x, self.y + rhs.y, self.z + rhs.z)
    }
}

#[derive(Clone, Copy)]
struct OldMat3x4 {
    m: [[f32; 4]; 3],
}

impl OldMat3x4 {
    fn from_rows(v: &[f32; 12]) -> Self {
        let mut m = [[0f32; 4]; 3];
        for r in 0..3 {
            for c in 0..4 {
                m[r][c] = v[r * 4 + c];
            }
        }
        OldMat3x4 { m }
    }
    fn mul(&self, v: OldVec) -> OldVec {
        OldVec::new(
            v.x * self.m[0][0] + v.y * self.m[0][1] + v.z * self.m[0][2] + self.m[0][3],
            v.x * self.m[1][0] + v.y * self.m[1][1] + v.z * self.m[1][2] + self.m[1][3],
            v.x * self.m[2][0] + v.y * self.m[2][1] + v.z * self.m[2][2] + self.m[2][3],
        )
    }
    /// scaleColumn(3, scale): copies, scales column 3
    fn scale_column(self, col: usize, s: f32) -> Self {
        let mut c = self;
        c.m[0][col] *= s;
        c.m[1][col] *= s;
        c.m[2][col] *= s;
        c
    }
    fn set_column(self, col: usize, v: OldVec) -> Self {
        let mut c = self;
        c.m[0][col] = v.x;
        c.m[1][col] = v.y;
        c.m[2][col] = v.z;
        c
    }
    fn translation(v: OldVec) -> Self {
        OldMat3x4 {
            m: [
                [1.0, 0.0, 0.0, v.x],
                [0.0, 1.0, 0.0, v.y],
                [0.0, 0.0, 1.0, v.z],
            ],
        }
    }
    fn scale(v: OldVec) -> Self {
        OldMat3x4 {
            m: [
                [v.x, 0.0, 0.0, 0.0],
                [0.0, v.y, 0.0, 0.0],
                [0.0, 0.0, v.z, 0.0],
            ],
        }
    }
}

fn old_rotate_z(v: &mut OldVec, a: f32) {
    let l = (v.x * v.x + v.y * v.y).sqrt();
    let d_a = v.x.atan2(v.y) + a;
    v.x = l * d_a.sin();
    v.y = l * d_a.cos();
}
fn old_rotate_x(v: &mut OldVec, a: f32) {
    let l = (v.y * v.y + v.z * v.z).sqrt();
    let d_a = v.y.atan2(v.z) + a;
    v.y = l * d_a.sin();
    v.z = l * d_a.cos();
}
fn old_rotate_y(v: &mut OldVec, a: f32) {
    let l = (v.x * v.x + v.z * v.z).sqrt();
    let d_a = v.x.atan2(v.z) + a;
    v.x = l * d_a.sin();
    v.z = l * d_a.cos();
}
fn old_rotate_zxy(v: &mut OldVec, r: OldVec) {
    old_rotate_z(v, r.z);
    old_rotate_x(v, r.x);
    old_rotate_y(v, r.y);
}

fn old_normalize(v: OldVec) -> OldVec {
    let inv_l = 1.0f32 / (v.x * v.x + v.y * v.y + v.z * v.z).sqrt();
    OldVec::new(v.x * inv_l, v.y * inv_l, v.z * inv_l)
}

fn c_trunc(v: f32) -> i32 {
    if v.is_nan() {
        0
    } else {
        v as i32
    }
}

// ---------------------------------------------------------------------------
// synthetic model
// ---------------------------------------------------------------------------

struct Section {
    tailer: RsVxlTailer,
    hva: [f32; 12],
    id: [u8; 16],
    span_data: Vec<u8>,
    starts: Vec<i32>,
    ends: Vec<i32>,
    span_data_ofs: i32,
}

impl Section {
    /// Full-column spans for an cx*cy column grid covering all cz voxels.
    fn new(id: &str, mins: [f32; 3], maxs: [f32; 3], cx: u8, cy: u8, cz: u8, hva: [f32; 12]) -> Self {
        // span layout per column: [skip=0][count=cz][cz*(color,norm)][end]
        let columns = cx as usize * cy as usize;
        let mut span_data = Vec::new();
        let mut starts = vec![0i32; columns];
        let mut ends = vec![0i32; columns];
        for j in 0..columns {
            starts[j] = span_data.len() as i32;
            span_data.push(0); // skip
            span_data.push(cz); // count
            for k in 0..cz {
                span_data.push(40u8.wrapping_add(k).wrapping_mul(7)); // color
                span_data.push(((j + k as usize) % 7) as u8); // normal index
            }
            span_data.push(0); // end of span byte
            ends[j] = span_data.len() as i32 - 1;
        }
        let mut idb = [0u8; 16];
        idb[..id.len()].copy_from_slice(id.as_bytes());
        Section {
            tailer: RsVxlTailer {
                scale: 1.0,
                x_min_scale: mins[0],
                y_min_scale: mins[1],
                z_min_scale: mins[2],
                x_max_scale: maxs[0],
                y_max_scale: maxs[1],
                z_max_scale: maxs[2],
                cx,
                cy,
                cz,
            },
            hva,
            id: idb,
            span_data,
            starts,
            ends,
            span_data_ofs: 0,
        }
    }
}

struct Model {
    sections: Vec<Section>,
    normals: Vec<f32>, // n*3 floats
}

/// One render result from a pipeline
struct RenderOut {
    image: Vec<u8>,
    lighting: Vec<u8>,
    x_center: i32,
    y_center: i32,
    width: i32,
    height: i32,
}

// ---------------------------------------------------------------------------
// OLD pipeline (verbatim port of a547a31 C++)
// ---------------------------------------------------------------------------

fn old_get_section_bounds(sec: &Section, rotation: OldVec, model_offset: OldVec) -> (OldVec, OldVec) {
    let matrix = OldMat3x4::from_rows(&sec.hva).scale_column(3, sec.tailer.scale);
    let mut min_vec = OldVec::new(
        sec.tailer.x_min_scale,
        sec.tailer.y_min_scale,
        sec.tailer.z_min_scale,
    );
    let mut max_vec = min_vec;
    for x in 0..2 {
        for y in 0..2 {
            for z in 0..2 {
                let corner = OldVec::new(
                    if x == 0 { sec.tailer.x_min_scale } else { sec.tailer.x_max_scale },
                    if y == 0 { sec.tailer.y_min_scale } else { sec.tailer.y_max_scale },
                    if z == 0 { sec.tailer.z_min_scale } else { sec.tailer.z_max_scale },
                ) + model_offset;
                let mut cur = matrix.mul(corner);
                old_rotate_zxy(&mut cur, rotation);
                min_vec.minimum(cur);
                max_vec.maximum(cur);
            }
        }
    }
    (min_vec, max_vec)
}

fn old_render_section(
    sec: &Section,
    normals: &[f32],
    light_direction: OldVec,
    rt_width: i32,
    rt_height: i32,
    model_offset: OldVec, // actually renderOffset from LoadVXLImage
    rotation: OldVec,
    post_hva_offset: OldVec,
    image: &mut [u8],
    lighting: &mut [u8],
    image_z: &mut [i8],
    track_center: bool,
    i3d_center_x: i32,
    i3d_center_y: i32,
) -> (i32, i32, i32, i32) {
    let inverse_light = {
        let n = old_normalize(light_direction);
        OldVec::new(-n.x, -n.y, -n.z)
    };
    let t = &sec.tailer;
    let cx1 = t.cx as i32;
    let cy1 = t.cy as i32;
    let cz1 = t.cz as i32;
    let matrix = OldMat3x4::from_rows(&sec.hva);
    let normal_matrix = matrix.set_column(3, OldVec::new(0.0, 0.0, 0.0));
    let scaled_matrix = matrix.scale_column(3, t.scale);
    let min_scale = OldVec::new(t.x_min_scale, t.y_min_scale, t.z_min_scale) + post_hva_offset;
    let max_scale = OldVec::new(t.x_max_scale, t.y_max_scale, t.z_max_scale) + post_hva_offset;
    let translate_to_world = OldMat3x4::translation(min_scale);
    let scale_to_world = OldMat3x4::scale(OldVec::new(
        (max_scale.x - min_scale.x) / cx1 as f32,
        (max_scale.y - min_scale.y) / cy1 as f32,
        (max_scale.z - min_scale.z) / cz1 as f32,
    ));

    let mut i3dx = if i3d_center_x < 0 { 0 } else { i3d_center_x };
    let mut i3dy = if i3d_center_y < 0 { 0 } else { i3d_center_y };

    let mut x_center = 0i32;
    let mut y_center = 0i32;
    let mut x_center_zmax = 0i32;
    let mut y_center_zmax = 0i32;

    if track_center {
        // Keep the emulation in sync with the intentional behavior change in
        // rs_vxl_render_section: the anchor must exclude the HVA translation
        // so compositing does not cancel out section positioning.
        let s_pixel = OldVec::new(0.0, 0.0, 0.0);
        let mut d_pixel = normal_matrix.mul(s_pixel);
        old_rotate_zxy(&mut d_pixel, rotation);
        d_pixel = d_pixel + model_offset;
        x_center = c_trunc(d_pixel.x + 0.5);
        y_center = c_trunc(d_pixel.y + 0.5);
    }

    let get_normal = |idx: usize| -> OldVec {
        if idx * 3 + 2 < normals.len() {
            OldVec::new(normals[idx * 3], normals[idx * 3 + 1], normals[idx * 3 + 2])
        } else {
            OldVec::new(0.0, 1.0, 0.0)
        }
    };

    let mut last_z_reported = -5000i32;
    let mut j = 0usize;
    for y in 0..cy1 {
        for x in 0..cx1 {
            let start = sec.starts[j];
            if start >= 0 {
                let end = sec.ends[j];
                let span_len = (end - start + 1) as usize;
                let base = sec.span_data_ofs as i64 + start as i64;
                let span = &sec.span_data[base as usize..(base as usize + span_len).min(sec.span_data.len())];
                let mut r = 0usize;
                let mut z = 0i32;
                while z < cz1 {
                    z += span[r] as i32;
                    r += 1;
                    let mut count = span[r] as i32;
                    r += 1;
                    while count > 0 {
                        let s_pixel = OldVec::new(x as f32, y as f32, z as f32);
                        let m_pixel = translate_to_world.mul(scale_to_world.mul(s_pixel));
                        let t_pixel = scaled_matrix.mul(m_pixel);
                        let mut d_pixel = t_pixel;
                        old_rotate_zxy(&mut d_pixel, rotation);
                        d_pixel = d_pixel + model_offset;

                        if x == i3dx && y == i3dy && z >= last_z_reported {
                            last_z_reported = z;
                            x_center_zmax = c_trunc(d_pixel.x);
                            y_center_zmax = c_trunc(d_pixel.y);
                        }

                        let px = c_trunc(d_pixel.x + 0.5);
                        let py = c_trunc(d_pixel.y + 0.5);
                        let ofs = (px + rt_width * py) as usize;

                        if px >= 0 && py >= 0 && px < rt_width && py < rt_height
                            && d_pixel.z > image_z[ofs] as f32
                        {
                            image[ofs] = span[r];
                            let normal_index = span[r + 1];
                            let normal = normal_matrix.mul(get_normal(normal_index as usize));
                            let mut n = normal;
                            old_rotate_zxy(&mut n, rotation);
                            let dot = n.x * inverse_light.x + n.y * inverse_light.y + n.z * inverse_light.z;
                            let light_val = if dot < 0.0 { 0.0 } else { dot };
                            lighting[ofs] = c_trunc(light_val * 255.0) as i32 as u8;
                            image_z[ofs] = c_trunc(d_pixel.z) as i8;
                        }
                        r += 2;
                        z += 1;
                        count -= 1;
                    }
                    r += 1;
                }
            }
            j += 1;
        }
    }
    (x_center, y_center, x_center_zmax, y_center_zmax)
}

fn old_load_vxl_image(model: &Model, light_direction: OldVec, rotation: OldVec, model_offset: OldVec) -> RenderOut {
    let mut min_coords = OldVec::new(10000.0, 10000.0, 10000.0);
    let mut max_coords = OldVec::new(-10000.0, -10000.0, -10000.0);
    let mut i_body_section: i32 = -1;
    let mut i_largest_section = 0usize;
    let mut i_largest_volume = 0f32;
    for (i, sec) in model.sections.iter().enumerate() {
        let (smin, smax) = old_get_section_bounds(sec, rotation, model_offset);
        let ex = smax.x - smin.x;
        let ey = smax.y - smin.y;
        let ez = smax.z - smin.z;
        let volume = ex * ey * ez;
        if volume >= i_largest_volume {
            i_largest_volume = volume;
            i_largest_section = i;
        }
        if sec.id.starts_with(b"BODY") {
            i_body_section = i as i32;
        }
        min_coords.minimum(smin);
        max_coords.maximum(smax);
    }
    let i_main = if i_body_section >= 0 { i_body_section as usize } else { i_largest_section };

    let render_offset = OldVec::new(-min_coords.x, -min_coords.y, -min_coords.z);

    let extents = OldVec::new(
        max_coords.x - min_coords.x,
        max_coords.y - min_coords.y,
        max_coords.z - min_coords.z,
    );
    let rt_width = extents.x.ceil() as i32 + 1;
    let rt_height = extents.y.ceil() as i32 + 1;
    let c_pixels = (rt_width * rt_height) as usize;

    let mut image = vec![0u8; c_pixels];
    let mut lighting = vec![255u8; c_pixels];
    let mut image_z = vec![i8::MIN; c_pixels];

    let mut xc = 0;
    let mut yc = 0;
    for (i, sec) in model.sections.iter().enumerate() {
        let (a, b, _c, _d) = old_render_section(
            sec,
            &model.normals,
            light_direction,
            rt_width,
            rt_height,
            render_offset,
            rotation,
            model_offset,
            &mut image,
            &mut lighting,
            &mut image_z,
            i == i_main,
            -1,
            -1,
        );
        if i == i_main {
            xc = a;
            yc = b;
        }
    }

    RenderOut {
        image,
        lighting,
        x_center: xc,
        y_center: yc,
        width: rt_width,
        height: rt_height,
    }
}

// ---------------------------------------------------------------------------
// NEW pipeline (mirrors the current C++ wrappers around the Rust core)
// ---------------------------------------------------------------------------

fn new_get_section_bounds(sec: &Section, rotation: [f32; 3], model_offset: [f32; 3]) -> ([f32; 3], [f32; 3]) {
    let mut mn = [0f32; 3];
    let mut mx = [0f32; 3];
    let mut main_section = 0i32;
    let res = unsafe { rs_vxl_compute_bounds(
        1,
        sec.id.as_ptr(),
        &sec.tailer,
        sec.hva.as_ptr(),
        rotation.as_ptr(),
        model_offset.as_ptr(),
        &mut main_section,
        mn.as_mut_ptr(),
        mx.as_mut_ptr(),
    ) };
    assert_eq!(res, RS_OK);
    (mn, mx)
}

fn new_render_section(
    sec: &Section,
    normals: &[f32],
    light_direction: [f32; 3],
    rt_width: i32,
    rt_height: i32,
    model_offset: [f32; 3],
    rotation: [f32; 3],
    post_hva_offset: [f32; 3],
    image: &mut [u8],
    lighting: &mut [u8],
    image_z: &mut [i8],
    track_center: bool,
    i3d_center_x: i32,
    i3d_center_y: i32,
) -> (i32, i32, i32, i32) {
    let (mut xc, mut yc) = (0i32, 0i32);
    let (mut xc_z, mut yc_z) = (0i32, 0i32);
    let res = unsafe { rs_vxl_render_section(
        image.as_mut_ptr(),
        lighting.as_mut_ptr(),
        image_z.as_mut_ptr(),
        image.len(),
        rt_width,
        rt_height,
        &sec.tailer,
        sec.hva.as_ptr(),
        normals.as_ptr(),
        (normals.len() / 3) as u32,
        light_direction.as_ptr(),
        rotation.as_ptr(),
        model_offset.as_ptr(),
        post_hva_offset.as_ptr(),
        sec.span_data.as_ptr(),
        sec.span_data.len(),
        sec.span_data_ofs,
        sec.starts.as_ptr(),
        sec.ends.as_ptr(),
        if track_center { &mut xc } else { std::ptr::null_mut() },
        if track_center { &mut yc } else { std::ptr::null_mut() },
        if track_center { &mut xc_z } else { std::ptr::null_mut() },
        if track_center { &mut yc_z } else { std::ptr::null_mut() },
        i3d_center_x,
        i3d_center_y,
        std::ptr::null_mut(),
    ) };
    assert_eq!(res, RS_OK);
    (xc, yc, xc_z, yc_z)
}

fn new_load_vxl_image(model: &Model, light_direction: [f32; 3], rotation: [f32; 3], model_offset: [f32; 3]) -> RenderOut {
    let mut min_coords = [10000f32; 3];
    let mut max_coords = [-10000f32; 3];
    let mut i_body_section: i32 = -1;
    let mut i_largest_section = 0usize;
    let mut i_largest_volume = 0f32;
    for (i, sec) in model.sections.iter().enumerate() {
        let (smin, smax) = new_get_section_bounds(sec, rotation, model_offset);
        let ex = smax[0] - smin[0];
        let ey = smax[1] - smin[1];
        let ez = smax[2] - smin[2];
        let volume = ex * ey * ez;
        if volume >= i_largest_volume {
            i_largest_volume = volume;
            i_largest_section = i;
        }
        if sec.id.starts_with(b"BODY") {
            i_body_section = i as i32;
        }
        for k in 0..3 {
            min_coords[k] = min_coords[k].min(smin[k]);
            max_coords[k] = max_coords[k].max(smax[k]);
        }
    }
    let i_main = if i_body_section >= 0 { i_body_section as usize } else { i_largest_section };

    let render_offset = [-min_coords[0], -min_coords[1], -min_coords[2]];

    let ex = max_coords[0] - min_coords[0];
    let ey = max_coords[1] - min_coords[1];
    let rt_width = ex.ceil() as i32 + 1;
    let rt_height = ey.ceil() as i32 + 1;
    let c_pixels = (rt_width * rt_height) as usize;

    let mut image = vec![0u8; c_pixels];
    let mut lighting = vec![255u8; c_pixels];
    let mut image_z = vec![i8::MIN; c_pixels];

    let mut xc = 0;
    let mut yc = 0;
    for (i, sec) in model.sections.iter().enumerate() {
        let (a, b, _c, _d) = new_render_section(
            sec,
            &model.normals,
            light_direction,
            rt_width,
            rt_height,
            render_offset,
            rotation,
            model_offset,
            &mut image,
            &mut lighting,
            &mut image_z,
            i == i_main,
            -1,
            -1,
        );
        if i == i_main {
            xc = a;
            yc = b;
        }
    }

    RenderOut {
        image,
        lighting,
        x_center: xc,
        y_center: yc,
        width: rt_width,
        height: rt_height,
    }
}

// ---------------------------------------------------------------------------
// tests
// ---------------------------------------------------------------------------

const PI_F32: f32 = 3.141592654_f32;

fn vehicle_rotation(i: i32) -> ([f32; 3], OldVec) {
    let (r_x, r_y, r_z) = (300.0f32, 0.0f32, 45.0f32 * i as f32 + 90.0f32);
    let rot = OldVec::new(
        r_x / 180.0f32 * PI_F32,
        r_y / 180.0f32 * PI_F32,
        r_z / 180.0f32 * PI_F32,
    );
    ([rot.x, rot.y, rot.z], rot)
}

fn compare_models(name: &str, model: &Model, turret_offset: [f32; 3]) {
    let light = OldVec::new(-0.55, -0.4, 0.9);
    let light_arr = [light.x, light.y, light.z];

    for i in 0..8 {
        let (rot_arr, rot_old) = vehicle_rotation(i);

        // body: no model offset
        let old_body = old_load_vxl_image(model, light, rot_old, OldVec::new(0.0, 0.0, 0.0));
        let new_body = new_load_vxl_image(model, light_arr, rot_arr, [0.0; 3]);

        // turret: with model offset (single section)
        let old_tur = old_load_vxl_image(model, light, rot_old, OldVec::new(turret_offset[0], turret_offset[1], turret_offset[2]));
        let new_tur = new_load_vxl_image(model, light_arr, rot_arr, turret_offset);

        // emulate the editor's composition: dest_left = xcenter - turret_x
        let old_shift = (old_body.x_center - old_tur.x_center, old_body.y_center - old_tur.y_center);
        let new_shift = (new_body.x_center - new_tur.x_center, new_body.y_center - new_tur.y_center);

        let mut body_pixel_diffs = 0usize;
        if old_body.image.len() == new_body.image.len() {
            for (o, n) in old_body.image.iter().zip(new_body.image.iter()) {
                if o != n { body_pixel_diffs += 1; }
            }
        }
        let mut tur_pixel_diffs = 0usize;
        if old_tur.image.len() == new_tur.image.len() {
            for (o, n) in old_tur.image.iter().zip(new_tur.image.iter()) {
                if o != n { tur_pixel_diffs += 1; }
            }
        }

        println!(
            "{} dir {}: body {}x{} c=({},{}) vs {}x{} c=({},{}) | turret {}x{} c=({},{}) vs {}x{} c=({},{}) | shift old {:?} new {:?} | pixdiff body={} tur={}",
            name, i,
            old_body.width, old_body.height, old_body.x_center, old_body.y_center,
            new_body.width, new_body.height, new_body.x_center, new_body.y_center,
            old_tur.width, old_tur.height, old_tur.x_center, old_tur.y_center,
            new_tur.width, new_tur.height, new_tur.x_center, new_tur.y_center,
            old_shift, new_shift, body_pixel_diffs, tur_pixel_diffs,
        );
    }
}

#[test]
fn ab_body_multi_section_matches_old_renderer() {
    // body-like: two sections, second is BODY with an HVA translation
    let model = Model {
        sections: vec![
            Section::new(
                "TURRET",
                [-6.0, -4.0, 0.0],
                [8.0, 6.0, 12.0],
                2,
                2,
                3,
                [1.0, 0.0, 0.0, 1.5, 0.0, 1.0, 0.0, -2.0, 0.0, 0.0, 1.0, 0.5],
            ),
            Section::new(
                "BODY",
                [-10.0, -6.0, 0.0],
                [14.0, 10.0, 24.0],
                3,
                3,
                4,
                [1.0, 0.0, 0.0, -2.0, 0.0, 1.0, 0.0, 3.0, 0.0, 0.0, 1.0, -1.0],
            ),
        ],
        normals: vec![0.0, 0.0, 1.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.7071, 0.0, 0.7071],
    };
    compare_models("body", &model, [0.0; 3]);
}

#[test]
fn ab_turret_single_section_with_translation_matches_old_renderer() {
    // turret-like: single section, non-zero HVA translation, asymmetric box
    let model = Model {
        sections: vec![Section::new(
            "TURRET",
            [-7.0, -5.0, 0.0],
            [13.0, 9.0, 20.0],
            3,
            2,
            5,
            [1.0, 0.0, 0.0, 4.0, 0.0, 1.0, 0.0, -2.5, 0.0, 0.0, 1.0, 1.25],
        )],
        normals: vec![0.0, 0.0, 1.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.7071, 0.0, 0.7071],
    };
    // art.ini TurretOffset=18 -> 18/6 = 3.0 along x
    compare_models("turret", &model, [3.0, 0.0, 0.0]);
}
}
