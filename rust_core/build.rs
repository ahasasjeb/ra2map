// Compiles the vendored minilzo (LZO 2.10) with all public symbols
// renamed (see vendor/minilzo/minilzo_rs.c) so the statically linked
// copy inside mission_editor_rust_core does not collide with lzo2.lib,
// which the editor links for the XCC library.
fn main() {
    let shader = std::fs::read_to_string("src/scene.wgsl").expect("read Vulkan scene shader");
    let module = naga::front::wgsl::parse_str(&shader).expect("parse Vulkan scene shader");
    let info = naga::valid::Validator::new(
        naga::valid::ValidationFlags::all(),
        naga::valid::Capabilities::IMMEDIATES,
    )
    .validate(&module)
    .expect("validate Vulkan scene shader");
    let words = naga::back::spv::write_vec(
        &module,
        &info,
        &naga::back::spv::Options::default(),
        Some(&naga::back::spv::PipelineOptions {
            shader_stage: naga::ShaderStage::Compute,
            entry_point: "main".into(),
        }),
    )
    .expect("compile Vulkan scene shader");
    let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_le_bytes()).collect();
    std::fs::write(
        std::path::Path::new(&std::env::var_os("OUT_DIR").unwrap()).join("scene.spv"),
        bytes,
    )
    .expect("write Vulkan scene SPIR-V");
    println!("cargo:rerun-if-changed=src/scene.wgsl");
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
