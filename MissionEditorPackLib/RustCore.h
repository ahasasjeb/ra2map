/*
    FinalSun/FinalAlert 2 Mission Editor

    Copyright (C) 1999-2024 Electronic Arts, Inc.

    C interface of the memory-safe Rust core (mission_editor_rust_core).
    The voxel rendering and TMP tile drawing kernels - the most
    memory-error-prone parts of the editor - live in Rust and are called
    through this header. All input buffers are bounds-checked inside the
    Rust core; malformed data is clamped/skipped instead of corrupting
    memory.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

#include <windows.h>

// Link directive: the actual .lib is produced by cargo (see the
// MissionEditorPackLib pre-build step) at
//   $(SolutionDir)rust_core\target\i686-pc-windows-msvc\release\mission_editor_rust_core.lib
#pragma comment(lib, "mission_editor_rust_core.lib")

// status codes
#define RS_OK 0
#define RS_ERR_BAD_ARG (-1)
#define RS_ERR_SMALL_BUFFER (-2)
#define RS_ERR_PANIC (-3)
#define RS_ERR_TOO_MANY_SECTIONS (-4)

// maximum sane render target dimension; bounds exceeding this are
// rejected by the C++ allocation code (guards against corrupt data)
#define RS_MAX_RENDER_TARGET_DIM 4096

#ifdef __cplusplus
extern "C"
{
#endif

// Mirrors the fields of t_vxl_section_tailer that the renderer needs.
typedef struct rs_vxl_tailer
{
    float scale;
    float x_min_scale;
    float y_min_scale;
    float z_min_scale;
    float x_max_scale;
    float y_max_scale;
    float z_max_scale;
    unsigned char cx;
    unsigned char cy;
    unsigned char cz;
} rs_vxl_tailer;

// Mirrors the fields of the TMP image header that the tile drawing needs.
typedef struct rs_tmp_tile_info
{
    int std_cx;    // f.get_cx()
    int std_cy;    // f.get_cy()
    int has_extra; // f.has_extra_graphics(i)
    int cx_extra;  // f.get_cx_extra(i)
    int cy_extra;  // f.get_cy_extra(i)
    int x_extra;   // f.get_x_extra(i) - f.get_x(i)
    int y_extra;   // f.get_y_extra(i) - f.get_y(i)
} rs_tmp_tile_info;

// Computes the overall projected bounds and the main section index of a
// VXL model (mirrors the bounds accumulation in LoadVXLImageInSurface /
// LoadVXLImage). section_ids points to section_count * 16 bytes (the
// raw char id[16] fields), hva_matrices to section_count * 12 floats.
int rs_vxl_compute_bounds(
    unsigned int section_count,
    const unsigned char* section_ids,
    const rs_vxl_tailer* tailers,
    const float* hva_matrices,
    const float* rotation,
    const float* model_offset,
    int* out_main_section,
    float* out_min, // 3 floats
    float* out_max  // 3 floats
);

// Renders one VXL section into caller-provided buffers (mirrors
// RenderVXLSection). image/lighting/image_z each hold buf_len bytes with
// buf_len >= rt_width * rt_height. span_data/span_data_len covers the
// whole section body (reads are clamped to it). span_start_list and
// span_end_list hold cx*cy int32 entries. p_last_op, when non-null,
// mirrors the C++ last_succeeded_operation values (10..13).
int rs_vxl_render_section(
    unsigned char* image,
    unsigned char* lighting,
    char* image_z,
    size_t buf_len,
    int rt_width,
    int rt_height,
    const rs_vxl_tailer* tailer,
    const float* hva_matrix, // 12 floats
    const float* normals,    // normal_count * 3 floats
    unsigned int normal_count,
    const float* light_direction,   // 3 floats
    const float* rotation,          // 3 floats
    const float* model_offset,      // 3 floats
    const float* post_hva_offset,   // 3 floats
    const unsigned char* span_data,
    size_t span_data_len,
    int span_data_ofs,
    const int* span_start_list,
    const int* span_end_list,
    int* out_center_x,
    int* out_center_y,
    int* out_center_x_zmax,
    int* out_center_y_zmax,
    int i3d_center_x,
    int i3d_center_y,
    int* p_last_op
);

// Returns the output dimensions (tile_cx, tile_cy) a TMP tile drawing
// will occupy (mirrors the size computation in tmp_ts_draw).
int rs_tmp_ts_get_size(
    const rs_tmp_tile_info* info,
    int* out_tile_cx,
    int* out_tile_cy
);

// Draws a TS TMP tile into dst (must hold at least the byte count
// returned by rs_tmp_ts_get_size). src must point to the raw image data
// of the tile (f.get_image(i)) and src_len must cover the remainder of
// the containing file so that reads can be clamped to valid memory.
int rs_tmp_ts_draw(
    unsigned char* dst,
    size_t dst_len,
    const unsigned char* src,
    size_t src_len,
    const rs_tmp_tile_info* info
);

#ifdef __cplusplus
}
#endif
