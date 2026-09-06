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
//   $(SolutionDir)rust_core\target\x86_64-pc-windows-msvc\release\mission_editor_rust_core.lib
#pragma comment(lib, "mission_editor_rust_core.lib")

// status codes
#define RS_OK 0
#define RS_ERR_BAD_ARG (-1)
#define RS_ERR_SMALL_BUFFER (-2)
#define RS_ERR_PANIC (-3)
#define RS_ERR_VULKAN_UNAVAILABLE (-10)
#define RS_ERR_VULKAN_RUNTIME (-11)
#define RS_ERR_LUA_RUNTIME (-20)

// maximum sane render target dimension; bounds exceeding this are
// rejected by the C++ allocation code (guards against corrupt data)
#define RS_MAX_RENDER_TARGET_DIM 4096

// sanity cap for the decoded size of a map pack (IsoMapPack5 / Format80
// pack). Real packs are a few MiB; a corrupt file can no longer drive a
// multi-GB allocation.
#define RS_MAX_PACK_DECODE_SIZE (256 * 1024 * 1024)

#ifdef __cplusplus
extern "C"
{
#endif

// Compact undo-snapshot codec. Call pack with a null output first to obtain
// the required size. Unpack succeeds only when it fills output_len exactly.
int rs_snapshot_pack(
    const unsigned char* input, size_t input_len,
    unsigned char* output, size_t output_cap, size_t* output_len);
int rs_snapshot_unpack(
    const unsigned char* input, size_t input_len,
    unsigned char* output, size_t output_len);

// ---------------------------------------------------------------------------
// Sandboxed Lua 5.5 map scripts. The host supplies a transactional view of
// the map INI through these callbacks and commits it only when rs_lua_run
// returns RS_OK. Lua receives no filesystem, process, package, or debug APIs.
// ---------------------------------------------------------------------------

typedef int (*rs_lua_get_callback)(
    void* context,
    const char* section,
    const char* key,
    char* dst,
    size_t dst_cap,
    size_t* out_len);

// section == NULL enumerates section names; otherwise enumerates keys in that
// section. The result is a sequence of NUL-terminated strings. A sizing call
// with dst == NULL returns the required byte count.
typedef size_t (*rs_lua_list_callback)(
    void* context,
    const char* section,
    char* dst,
    size_t dst_cap);

// Operations: 0=set key, 1=remove key, 2=clear section, 3=remove section.
typedef int (*rs_lua_mutate_callback)(
    void* context,
    int operation,
    const char* section,
    const char* key,
    const char* value);

typedef void (*rs_lua_print_callback)(void* context, const char* text);

// Invokes a whitelisted editor operation. Arguments are UTF-8 strings and
// the optional response is UTF-8 text. The callback is invoked exactly once;
// responses larger than dst_cap must be rejected instead of requiring a
// sizing call because some operations display UI or stage a mutation.
typedef int (*rs_lua_invoke_callback)(
    void* context,
    const char* operation,
    const char* const* args,
    size_t arg_count,
    char* dst,
    size_t dst_cap,
    size_t* out_len);

typedef struct rs_lua_callbacks
{
    rs_lua_get_callback get;
    rs_lua_list_callback list;
    rs_lua_mutate_callback mutate;
    rs_lua_print_callback print;
    rs_lua_invoke_callback invoke;
} rs_lua_callbacks;

int rs_lua_run(
    const unsigned char* source,
    size_t source_len,
    const char* source_name,
    const rs_lua_callbacks* callbacks,
    void* context,
    char* error,
    size_t error_cap);

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

// ---------------------------------------------------------------------------
// Pack codecs (base64 / Format80 / Format5-LZO). The C++ originals walked
// raw pointers with file-provided lengths; these Rust ports are fully
// bounds-checked and cannot corrupt the heap on malformed input.
//
// Buffer convention: pass dst = NULL / dst_cap = 0 to only measure; the
// required size is reported through out_len and RS_ERR_SMALL_BUFFER is
// returned. Otherwise RS_OK on success and RS_ERR_BAD_ARG on malformed
// input.
// ---------------------------------------------------------------------------

// Standard base64; output is NOT NUL-terminated.
int rs_base64_encode(
    const unsigned char* src, size_t src_len,
    unsigned char* dst, size_t dst_cap, size_t* out_len);

// Decodes a NUL-terminated base64 string (stops at the first NUL byte,
// mirroring xcc decode64).
int rs_base64_decode(
    const char* src, size_t src_len,
    unsigned char* dst, size_t dst_cap, size_t* out_len);

// Raw Format80 encode (mirrors xcc encode80).
int rs_f80_encode(
    const unsigned char* src, size_t src_len,
    unsigned char* dst, size_t dst_cap, size_t* out_len);

// Sectioned Format80 (FSunPackLib::EncodeF80 layout): n_sections
// sections of src_len / n_sections bytes.
int rs_f80_pack_encode(
    const unsigned char* src, size_t src_len, unsigned int n_sections,
    unsigned char* dst, size_t dst_cap, size_t* out_len);

// Sectioned Format80 decode (FSunPackLib::DecodeF80). max_size caps the
// declared output size (mirrors the original totalSize check).
int rs_f80_pack_decode(
    const unsigned char* src, size_t src_len,
    unsigned char* dst, size_t dst_cap, size_t max_size, size_t* out_len);

// Format5/LZO with 8192-byte sections (FSunPackLib::EncodeIsoMapPack5).
int rs_pack5_encode(
    const unsigned char* src, size_t src_len,
    unsigned char* dst, size_t dst_cap, size_t* out_len);

// Format5/LZO decode (FSunPackLib::DecodeIsoMapPack5). max_size caps the
// declared output size; sections that would overflow are rejected
// instead of corrupting the heap.
int rs_pack5_decode(
    const unsigned char* src, size_t src_len,
    unsigned char* dst, size_t dst_cap, size_t max_size, size_t* out_len);

// ---------------------------------------------------------------------------
// RA2/YR CSF string table parsing (CLoading::LoadStrings). The C++
// original trusted every file-provided length field; this parser clamps
// all reads to the buffer and reports truncation instead.
// ---------------------------------------------------------------------------

// One parsed string table entry. ids / values / values_asc hold the
// concatenated byte blobs in entry order (values are decoded UTF-16LE
// code units, 2 bytes per value_len).
typedef struct rs_csf_entry
{
    unsigned int id_len;         // bytes (ASCII)
    unsigned int value_len;      // UTF-16 code units
    unsigned int value_asc_len;  // bytes (ASCII), 0 when absent
} rs_csf_entry;

// Parses a whole CSF file into flat caller-provided buffers. Call once
// with NULL buffers to obtain the sizes (returns RS_ERR_SMALL_BUFFER),
// then again with suitably sized buffers. Returns RS_ERR_BAD_ARG when
// the file does not contain the " FSC" marker or a complete header.
// out_truncated (optional) is set to 1 when the file ended before all
// declared entries could be parsed.
int rs_csf_parse(
    const unsigned char* data, size_t data_len,
    rs_csf_entry* entries, size_t entry_cap, size_t* out_entry_count,
    unsigned char* ids, size_t ids_cap, size_t* out_ids_len,
    unsigned char* values, size_t values_cap, size_t* out_values_len,
    unsigned char* values_asc, size_t values_asc_cap, size_t* out_values_asc_len,
    int* out_truncated
);

// ---------------------------------------------------------------------------
// Vulkan presentation backend. Rust owns the Vulkan instance/device,
// swapchain, synchronization primitives and persistently mapped upload memory.
// The legacy CPU rasterizer supplies a read-only final pixel view.
// ---------------------------------------------------------------------------

typedef struct rs_vulkan_renderer rs_vulkan_renderer;

int rs_vulkan_create(void* hwnd, rs_vulkan_renderer** out_renderer);

void rs_vulkan_destroy(rs_vulkan_renderer* renderer);

// Reserves the swapchain and mapped upload storage before the legacy editor
// loads its large graphics cache. Delaying this allocation until the first
// frame can exhaust address space in the 32-bit process on large modded maps.
int rs_vulkan_prepare(
    rs_vulkan_renderer* renderer,
    int target_width,
    int target_height,
    int vsync
);

// Presents src_width/src_height pixels from the source rectangle, scaling to
// the Vulkan swapchain extent. Pixel masks describe the little-endian source
// format used by the DirectDraw-compatible CPU raster surface.
int rs_vulkan_present(
    rs_vulkan_renderer* renderer,
    const unsigned char* pixels,
    int surface_width,
    int surface_height,
    int pitch,
    unsigned int bytes_per_pixel,
    unsigned int red_mask,
    unsigned int green_mask,
    unsigned int blue_mask,
    int src_left,
    int src_top,
    int src_width,
    int src_height,
    int target_width,
    int target_height,
    int vsync
);

// Copies the most recent Vulkan diagnostic to dst and always NUL-terminates
// when dst_cap is non-zero. Returns the full diagnostic byte count.
size_t rs_vulkan_last_error(char* dst, size_t dst_cap);

// GPU scene commands: geometry only. Immutable indexed sprites and palettes
// are uploaded once; the compute compositor performs all pixel work.
typedef struct rs_scene_command {
    int rect[4];
    int clip[4];
    unsigned int data[4];
    unsigned int tint[4];
    int line[4];
    unsigned int extra[4];
} rs_scene_command;
int rs_vulkan_scene_upload(rs_vulkan_renderer*, uint64_t key, const unsigned int* words, size_t count, unsigned int* offset);
int rs_vulkan_scene_present(rs_vulkan_renderer*, const rs_scene_command*, size_t count,
    float left, float top, float scale_x, float scale_y, unsigned int width, unsigned int height, int vsync);
size_t rs_vulkan_info(const rs_vulkan_renderer*, char* dst, size_t cap);
int rs_vulkan_scene_reset(rs_vulkan_renderer*);

#ifdef __cplusplus
}
#endif
