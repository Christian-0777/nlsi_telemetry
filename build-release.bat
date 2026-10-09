@echo off
setlocal

cd /d "%~dp0"
if errorlevel 1 (
    echo [ERROR] Could not change to the project directory.
    exit /b 1
)

for /f "usebackq delims=" %%V in (`powershell.exe -NoProfile -Command "(Get-Content -Raw version.json | ConvertFrom-Json).version"`) do set "APP_VERSION=%%V"
for /f "usebackq delims=" %%C in (`powershell.exe -NoProfile -Command "(Get-Content -Raw version.json | ConvertFrom-Json).channel"`) do set "APP_CHANNEL=%%C"
if not defined APP_VERSION (
    echo [ERROR] Could not read the application version from version.json.
    exit /b 2
)
if not defined APP_CHANNEL (
    echo [ERROR] Could not read the release channel from version.json.
    exit /b 2
)

set "RELEASE_TAG=v%APP_VERSION%"
if /i not "%APP_CHANNEL%"=="stable" set "RELEASE_TAG=v%APP_VERSION%-%APP_CHANNEL%"
set "BUILD_DIR=%CD%\build\cmake-%RELEASE_TAG%"
set "RELEASE_DIR=%CD%\build\releases\%RELEASE_TAG%"
set "INSTALLER_PATH=%RELEASE_DIR%\NLSI-Exclusive-Logbook-%RELEASE_TAG%-Setup.exe"

if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo [ERROR] Native CMake build directory is not configured:
    echo         "%BUILD_DIR%"
    echo Configure the version-specific VS 2022 x64 CMake build without reusing v1.4.1 outputs.
    exit /b 2
)

echo ========================================
echo  NLSI EXCLUSIVE LOGBOOK v%APP_VERSION%-%APP_CHANNEL%
echo ========================================
echo.

echo [1/4] Building native Release application...
cmake --build "%BUILD_DIR%" --config Release --target NLSI-Exclusive-Logbook nlsi_core_tests nlsi_gui_tests
if errorlevel 1 (
    echo [ERROR] Native Release build failed.
    exit /b %ERRORLEVEL%
)
if not exist "%RELEASE_DIR%\NLSI-Exclusive-Logbook.exe" (
    echo [ERROR] Native Release executable was not created:
    echo         "%RELEASE_DIR%\NLSI-Exclusive-Logbook.exe"
    exit /b 1
)

echo [2/4] Running Python compatibility and packaging tests...
py -3 -m unittest discover -s test -v
if errorlevel 1 (
    echo [ERROR] Python tests failed.
    exit /b %ERRORLEVEL%
)

echo [3/4] Running native Release CTest...
ctest --test-dir "%BUILD_DIR%" -C Release --output-on-failure
if errorlevel 1 (
    echo [ERROR] Native Release CTest failed.
    exit /b %ERRORLEVEL%
)

echo [4/4] Building the native installer...
call "%CD%\build-installer.bat"
if errorlevel 1 (
    echo [ERROR] Native installer build failed.
    exit /b %ERRORLEVEL%
)
if not exist "%INSTALLER_PATH%" (
    echo [ERROR] Installer was not created:
    echo         "%INSTALLER_PATH%"
    exit /b 1
)

echo.
echo ========================================
echo  NLSI EXCLUSIVE LOGBOOK RELEASE BUILD COMPLETE
echo ========================================
echo.
echo Application:
echo %RELEASE_DIR%\NLSI-Exclusive-Logbook.exe
echo.
echo Installer:
echo %INSTALLER_PATH%
echo.
exit /b 0
