@echo off
echo This script is a convenience script for building a full FinalSun and FinalAlert 2 YR distribution. It is not required for daily development.

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
  call "%VS_PATH%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
)


@echo on
pushd "%~dp0\.."
rmdir /Q /S "%~dp0..\3rdParty\xcc\vcpkg_installed"
msbuild "-p:VcpkgAdditionalInstallOptions=--binarysource clear """--downloads-root=%~dp0..\3rdParty\xcc\vcpkg_downloads""" """--x-buildtrees-root=%~dp0..\3rdParty\xcc\vcpkg_installed\x64-windows\_buildtrees"""" "-p:Configuration=FinalAlertRelease YR" -p:Platform=x64 -p:DistributeMissionEditor=true /t:Rebuild MissionEditor.sln
msbuild "-p:VcpkgAdditionalInstallOptions=--binarysource clear """--downloads-root=%~dp0..\3rdParty\xcc\vcpkg_downloads""" """--x-buildtrees-root=%~dp0..\3rdParty\xcc\vcpkg_installed\x64-windows\_buildtrees"""" "-p:Configuration=FinalAlertRelease" -p:Platform=x64 -p:DistributeMissionEditor=true /t:Rebuild MissionEditor.sln
msbuild "-p:VcpkgAdditionalInstallOptions=--binarysource clear """--downloads-root=%~dp0..\3rdParty\xcc\vcpkg_downloads""" """--x-buildtrees-root=%~dp0..\3rdParty\xcc\vcpkg_installed\x64-windows\_buildtrees"""" "-p:Configuration=FinalSunRelease" -p:Platform=x64 -p:DistributeMissionEditor=true /t:Rebuild MissionEditor.sln

popd

pushd "%~dp0"
call zip_sources.bat
call zip_3rdParty_sources.bat
popd
