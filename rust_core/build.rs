// Compiles the vendored minilzo (LZO 2.10) with all public symbols
// renamed (see vendor/minilzo/minilzo_rs.c) so the statically linked
// copy inside mission_editor_rust_core does not collide with lzo2.lib,
// which the editor links for the XCC library.
fn main() {
    cc::Build::new()
        .file("vendor/minilzo/minilzo_rs.c")
        .include("vendor/minilzo")
        .warnings(false)
        .compile("minilzo_rs");

    println!("cargo:rerun-if-changed=vendor/minilzo/minilzo_rs.c");
    println!("cargo:rerun-if-changed=vendor/minilzo/minilzo.c");
    println!("cargo:rerun-if-changed=vendor/minilzo/minilzo.h");
    println!("cargo:rerun-if-changed=vendor/minilzo/lzoconf.h");
    println!("cargo:rerun-if-changed=vendor/minilzo/lzodefs.h");
}
