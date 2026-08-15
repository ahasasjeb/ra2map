/*
    minilzo (LZO 2.10) compiled with all public symbols renamed.

    The editor links lzo2.lib (the import library of lzo2.dll) for the
    XCC library, so the statically linked copy of minilzo inside
    mission_editor_rust_core must not export the same symbols - the
    renames below keep the two implementations from colliding while
    preserving identical compression output (same upstream source).

    LZO is distributed under the GNU General Public License (GPL v2+);
    see README.LZO in this directory.
*/

#define __lzo_ptr_linear        rs_core___lzo_ptr_linear
#define __lzo_align_gap         rs_core___lzo_align_gap
#define lzo_copyright           rs_core_lzo_copyright
#define lzo_version             rs_core_lzo_version
#define lzo_version_string      rs_core_lzo_version_string
#define lzo_version_date        rs_core_lzo_version_date
#define _lzo_version_string     rs_core__lzo_version_string
#define _lzo_version_date       rs_core__lzo_version_date
#define lzo_adler32             rs_core_lzo_adler32
#define _lzo_config_check       rs_core__lzo_config_check
#define __lzo_init_v2           rs_core___lzo_init_v2
#define lzo1x_1_compress        rs_core_lzo1x_1_compress
#define lzo1x_decompress        rs_core_lzo1x_decompress
#define lzo1x_decompress_safe   rs_core_lzo1x_decompress_safe

#include "minilzo.c"
