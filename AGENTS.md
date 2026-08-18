# Repository Guidelines

## Project Structure & Module Organization

`MissionEditor/` is the MFC/C++23 desktop editor. Most features are a matching
`Name.h`/`Name.cpp` pair; shared resources and distributable game data live in
`MissionEditor/data/`, while build configuration is in `MissionEditor/PropertySheets/`.
`MissionEditorPackLib/` exposes C-compatible packing and loading helpers used by the
editor. `rust_core/` builds the `mission_editor_rust_core` static library for safe
codec and Vulkan-related code. Treat `3rdParty/xcc/` as vendored code: update it only
when deliberately importing an upstream change and preserve its notices. `scripts/`
contains packaging and maintenance utilities. Generated output belongs in `build/`,
`dist/`, or `rust_core/target/` and must not be committed.

## Build, Test, and Development Commands

Use a Visual Studio Developer PowerShell with the Desktop C++ workload, MFC/ATL,
the Visual Studio vcpkg component, and Rust installed. From the repository root:

```powershell
msbuild MissionEditor.sln /p:Configuration="FinalAlertDebug YR" /p:Platform=x64 /m
msbuild MissionEditor.sln /p:Configuration="Tests FinalAlertDebug YR" /p:Platform=x64 /m
.\dist\FinalAlert2YR\FinalAlert2YRTestsd.exe
```

The first command builds the debug Yuri's Revenge editor; the second produces and
runs the C++ test executable (it must report zero failures). Run Rust tests separately:

```powershell
cd rust_core
cargo test --target x86_64-pc-windows-msvc
```

`scripts\build_and_distribute.bat` rebuilds all release variants and creates archives;
use it only when preparing a distribution.

## Toolchain and Configuration Notes

All supported solution configurations target `x64`. Use the exact names from
`MissionEditor.sln`: `FinalSunDebug`, `FinalSunRelease`, `FinalAlertDebug`,
`FinalAlertRelease`, `FinalAlertDebug YR`, `FinalAlertRelease YR`, and
`Tests FinalAlertDebug YR`. Their runnable output directories are, respectively,
`dist/FinalSun`, `dist/FinalAlert2`, and `dist/FinalAlert2YR`.

`3rdParty/xcc/vcpkg.json` is a manifest consumed by MSBuild; allow vcpkg to restore
its dependencies instead of committing `vcpkg_installed/` or `vcpkg_downloads/`.
Building `MissionEditorPackLib` automatically invokes
`cargo build --release --target x86_64-pc-windows-msvc`, even for a debug C++ build.
Keep the Rust target installed and do not assume the editor consumes a debug Rust
library.

## Coding Style & Naming Conventions

Follow the surrounding code. C++ uses tabs for indentation, braces on the next line,
PascalCase types/functions, and `m_`-prefixed member fields (for example, `CTube` and
`m_tubeParts`). Include `StdAfx.h` first in MFC implementation files and keep related
declarations and definitions together. Rust uses `rustfmt`-style four-space indentation,
`snake_case` functions, and explicit checked boundary handling at the C FFI boundary.
No formatter or linter is enforced; avoid drive-by reformatting.
Preserve the existing line endings when editing existing files.

## Testing Guidelines

Add focused checks to `MissionEditor/tests.cpp` using the existing `test_*` methods and
`TEST`/`REPORT_TEST` macros, then register the method in `Tests::run()`. Add Rust unit
tests next to the code they cover and run the target-specific command above. Manually
smoke-test UI or rendering changes in the relevant FinalSun/FinalAlert configuration.

## Commit & Pull Request Guidelines

Recent commits use short, imperative summaries, commonly in Chinese (for example,
`优化 Vulkan 像素转换性能`); keep each commit scoped to one change. PRs should explain
the affected editor variant, link any issue, list build/test commands run, and include
screenshots for UI or rendering changes. Call out data, third-party, or distribution
changes explicitly.
