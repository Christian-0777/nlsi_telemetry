@echo off
setlocal
cd /d "%~dp0"

if "%~1"=="" (
    echo Usage: build.bat ^<path-to-extracted-scs-sdk-1.15^>
    exit /b 2
)

for /f "usebackq delims=" %%V in (`powershell.exe -NoProfile -Command "(Get-Content -Raw version.json | ConvertFrom-Json).version"`) do set "APP_VERSION=%%V"
if errorlevel 1 (
    echo [ERROR] Could not read the version from version.json.
    exit /b 1
)
if not defined APP_VERSION (
    echo [ERROR] version.json does not contain a version.
    exit /b 1
)

set "SDK_ROOT=%~f1"
if not exist "%SDK_ROOT%\include\scssdk_telemetry.h" (
    echo The SDK root must contain include\scssdk_telemetry.h.
    exit /b 2
)

if "%~2"=="" (
    set "BUILD_ROOT=%CD%\build\v%APP_VERSION%"
) else (
    set "BUILD_ROOT=%~f2"
)
if not exist "%BUILD_ROOT%" mkdir "%BUILD_ROOT%"
if errorlevel 1 (
    echo Could not create the build output directory: "%BUILD_ROOT%"
    exit /b 1
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo Visual Studio Build Tools was not found.
    exit /b 2
)

for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_INSTALL=%%I"
if not defined VS_INSTALL (
    echo The Visual C++ x64 tools are not installed.
    exit /b 2
)

call "%VS_INSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 1

echo Compiler source: src\nlsi_telemetry.cpp
cl /nologo /std:c++17 /EHsc /W4 /MT /LD /I "%SDK_ROOT%\include" /Fo"%BUILD_ROOT%\nlsi_telemetry.obj" src\nlsi_telemetry.cpp /link /DEF:src\nlsi_telemetry.def /OUT:"%BUILD_ROOT%\nlsi_telemetry.dll" /IMPLIB:"%BUILD_ROOT%\nlsi_telemetry.lib" Ws2_32.lib
if errorlevel 1 exit /b 1

echo Built %BUILD_ROOT%\nlsi_telemetry.dll
