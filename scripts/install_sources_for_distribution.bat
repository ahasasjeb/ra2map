@echo off
echo This script not only installs the vcpkg packages (this is usually done automatically when building), but also makes the downloaded sources and the build sources (with VCPKG patches applied) available in this repository. This may be helpful for distributing, archiving and license compliance and is not required for daily development.

if "%VCINSTALLDIR%" == "" (
  set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
  set "VS_PATH="
  if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_PATH=%%i"
  )
  if "%VS_PATH%" == "" if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" set "VS_PATH=%ProgramFiles%\Microsoft Visual Studio\2022\Community"
  if "%VS_PATH%" == "" (
    echo Could not locate a Visual Studio installation with the C++ workload. Please install Visual Studio 2022 or newer or activate a developer command prompt.
    exit /b 1
  )
  call "%VS_PATH%\Common7\Tools\VsDevCmd.bat" -arch=x86 -host_arch=x86
  if exist "%VS_PATH%\VC\vcpkg\vcpkg.exe" set "PATH=%VS_PATH%\VC\vcpkg;%PATH%"
)

@echo on
pushd "%~dp0..\3rdParty\xcc"
rmdir /Q /S "%~dp0..\3rdParty\xcc\vcpkg_installed"
vcpkg install --binarysource clear "--downloads-root=%~dp0..\3rdParty\xcc\vcpkg_downloads" "--x-install-root=%~dp0..\3rdParty\xcc\vcpkg_installed\x86-windows" "--x-buildtrees-root=%~dp0..\3rdParty\xcc\vcpkg_installed\x86-windows\_buildtrees" --triplet x86-windows
popd
