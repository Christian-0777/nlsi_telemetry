@echo off
setlocal

cd /d "%~dp0"
if errorlevel 1 (
    echo [ERROR] Could not change to the project directory.
    exit /b 1
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

echo ========================================
echo  NLSI TELEMETRY v%APP_VERSION% BUILD
echo ========================================
echo.

set "CLEAN_BUILD=0"
set "SDK_ROOT=C:\SCS\scs_sdk_1_15"

if /i "%~1"=="clean" goto clean_argument
if not "%~1"=="" goto sdk_argument
goto validate_sdk

:clean_argument
set "CLEAN_BUILD=1"
if "%~2"=="" goto validate_sdk
if not "%~3"=="" goto usage_error
set "SDK_ROOT=%~f2"
goto validate_sdk

:sdk_argument
if not "%~2"=="" goto usage_error
set "SDK_ROOT=%~f1"

:validate_sdk
echo [1/4] Validating SCS SDK...
if not exist "%SDK_ROOT%\include\scssdk_telemetry.h" (
    echo [ERROR] SCS SDK header not found:
    echo         "%SDK_ROOT%\include\scssdk_telemetry.h"
    exit /b 2
)
echo [OK] SDK found: "%SDK_ROOT%"

if "%CLEAN_BUILD%"=="1" (
    echo [INFO] Removing generated build artifacts...
    if exist "%CD%\build" rmdir /s /q "%CD%\build"
    if errorlevel 1 (
        echo [ERROR] Could not remove the generated build directory.
        exit /b 1
    )
    echo [OK] Generated build artifacts removed
)

echo [2/4] Building telemetry DLL...
call "%CD%\build.bat" "%SDK_ROOT%"
if errorlevel 1 (
    echo [ERROR] DLL compilation failed.
    exit /b %ERRORLEVEL%
)
if not exist "%CD%\build\nlsi_telemetry.dll" (
    echo [ERROR] DLL build reported success, but build\nlsi_telemetry.dll was not created.
    exit /b 1
)
echo [OK] DLL built

echo [3/4] Running tests...
py -3 -m unittest discover -s test -v
if errorlevel 1 (
    echo [ERROR] Python tests failed. The installer will not be built.
    exit /b %ERRORLEVEL%
)
echo [OK] All tests passed

echo [4/4] Building installer...
call "%CD%\build-installer.bat" "%SDK_ROOT%"
if errorlevel 1 (
    echo [ERROR] Installer build failed.
    exit /b %ERRORLEVEL%
)

set "INSTALLER_PATH=%CD%\build\v%APP_VERSION%\NLSI-Telemetry-Setup-v%APP_VERSION%.exe"
set "BUILD_INFO_PATH=%CD%\build\v%APP_VERSION%\build-info.json"
if not exist "%INSTALLER_PATH%" (
    echo [ERROR] Installer build reported success, but the expected installer was not created:
    echo         "%INSTALLER_PATH%"
    exit /b 1
)
if not exist "%BUILD_INFO_PATH%" (
    echo [ERROR] Installer build reported success, but build-info.json was not created:
    echo         "%BUILD_INFO_PATH%"
    exit /b 1
)
echo [OK] Installer built

echo.
echo ========================================
echo  NLSI TELEMETRY v%APP_VERSION% BUILD COMPLETE
echo ========================================
echo.
echo Installer:
echo %INSTALLER_PATH%
echo.
echo Build info:
echo %BUILD_INFO_PATH%
echo.
echo DLL:
echo %CD%\build\nlsi_telemetry.dll
echo.
echo ========================================
exit /b 0

:usage_error
echo Usage: build-release.bat [clean] [path-to-extracted-scs-sdk-1.15]
echo        build-release.bat "D:\somewhere\scs_sdk_1_15"
echo        build-release.bat clean "D:\somewhere\scs_sdk_1_15"
exit /b 2
