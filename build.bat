@echo off
setlocal
cd /d "%~dp0"

if "%~1"=="" (
    echo Usage: build.bat ^<path-to-extracted-scs-sdk-1.15^>
    exit /b 2
)

set "SDK_ROOT=%~f1"
if not exist "%SDK_ROOT%\include\scssdk_telemetry.h" (
    echo The SDK root must contain include\scssdk_telemetry.h.
    exit /b 2
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

if not exist "build" mkdir "build"
cl /nologo /std:c++17 /EHsc /W4 /MT /LD /I "%SDK_ROOT%\include" /Fo:build\ src\nlsi_telemetry.cpp /link /DEF:src\nlsi_telemetry.def /OUT:build\nlsi_telemetry.dll /IMPLIB:build\nlsi_telemetry.lib Ws2_32.lib
if errorlevel 1 exit /b 1

echo Built build\nlsi_telemetry.dll
