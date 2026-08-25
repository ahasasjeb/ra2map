$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Path $PSScriptRoot -Parent
$rustCoreRoot = Join-Path -Path $repositoryRoot -ChildPath 'rust_core'
$manifestPath = Join-Path -Path $rustCoreRoot -ChildPath 'Cargo.toml'
$targetRoot = Join-Path -Path $rustCoreRoot -ChildPath 'target\x86_64-pc-windows-msvc\release'
$cargoCommand = Get-Command -Name cargo -ErrorAction SilentlyContinue

if ($null -eq $cargoCommand) {
    $fallbackCargo = Join-Path -Path $env:USERPROFILE -ChildPath '.cargo\bin\cargo.exe'
    if (-not (Test-Path -LiteralPath $fallbackCargo -PathType Leaf)) {
        throw 'Cargo was not found in PATH or the standard user installation directory.'
    }
    $cargoExecutable = $fallbackCargo
} else {
    $cargoExecutable = $cargoCommand.Source
}

& $cargoExecutable build --release --target x86_64-pc-windows-msvc --manifest-path $manifestPath
if ($LASTEXITCODE -ne 0) {
    throw "Rust core build failed with exit code $LASTEXITCODE."
}

$cargoBuildRoot = Join-Path -Path $targetRoot -ChildPath 'build'
$luaLibraries = @(
    Get-ChildItem -LiteralPath $cargoBuildRoot -Filter 'lua5.4.lib' -File -Recurse |
        Where-Object { $_.FullName -match '[\\/]mlua-sys-[^\\/]+[\\/]out[\\/]lib[\\/]lua5\.4\.lib$' } |
        Sort-Object -Property LastWriteTimeUtc -Descending
)
if ($luaLibraries.Count -eq 0) {
    throw "Cargo completed but the vendored Lua 5.4 static library was not found under $cargoBuildRoot."
}

$stableLuaLibrary = Join-Path -Path $targetRoot -ChildPath 'lua5.4.lib'
Copy-Item -LiteralPath $luaLibraries[0].FullName -Destination $stableLuaLibrary -Force
