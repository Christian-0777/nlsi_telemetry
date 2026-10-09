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

set "BUILD_DIR=%CD%\build\cmake"
set "RELEASE_DIR=%CD%\build\releases\v%APP_VERSION%-%APP_CHANNEL%"
set "INSTALLER_PATH=%RELEASE_DIR%\NLSI-Exclusive-Logbook-v%APP_VERSION%-%APP_CHANNEL%-Setup.exe"

if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo [ERROR] Native CMake build directory is not configured:
    echo         "%BUILD_DIR%"
    echo Configure the existing VS 2022 x64 CMake build without reusing build\.
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
