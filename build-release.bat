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
set "X86_PLUGIN_BUILD_DIR=%CD%\build\cmake-scs-position-%RELEASE_TAG%-x86"
set "RELEASE_DIR=%CD%\build\releases\%RELEASE_TAG%"
set "PLUGIN_RELEASE_DIR=%CD%\build\plugins\%RELEASE_TAG%"
set "INSTALLER_PATH=%RELEASE_DIR%\NLSI-Exclusive-Logbook-%RELEASE_TAG%-Setup.exe"
set "QT_PREFIX=%CMAKE_PREFIX_PATH%"
if not defined QT_PREFIX if exist "C:\Qt\6.12.0\msvc2022_64\lib\cmake\Qt6\Qt6Config.cmake" (
    set "QT_PREFIX=C:\Qt\6.12.0\msvc2022_64"
)

if not defined QT_PREFIX (
    echo [ERROR] Qt 6.12 for MSVC 2022 was not found. Set CMAKE_PREFIX_PATH to the Qt installation prefix.
    exit /b 2
)
echo Configuring the version-specific VS 2022 x64 native build...
cmake -S "%CD%" -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON -DCMAKE_PREFIX_PATH="%QT_PREFIX%"
if errorlevel 1 (
    echo [ERROR] Native x64 CMake configuration failed.
    exit /b 1
)

echo ========================================
echo  NLSI EXCLUSIVE LOGBOOK v%APP_VERSION%-%APP_CHANNEL%
echo ========================================
echo.

echo [1/5] Building native Release application and x64 position plugin...
cmake --build "%BUILD_DIR%" --config Release --target NLSI-Exclusive-Logbook nlsi nlsi_core_tests nlsi_gui_tests
if errorlevel 1 (
    echo [ERROR] Native Release application or x64 plugin build failed.
    exit /b 1
)
if not exist "%RELEASE_DIR%\NLSI-Exclusive-Logbook.exe" (
    echo [ERROR] Native Release executable was not created:
    echo         "%RELEASE_DIR%\NLSI-Exclusive-Logbook.exe"
    exit /b 1
)

echo Configuring the version-specific Win32 SCS position plugin build...
cmake -S "%CD%\scs_position_plugin" -B "%X86_PLUGIN_BUILD_DIR%" -G "Visual Studio 17 2022" -A Win32 -DNLSI_RELEASE_TAG=%RELEASE_TAG%
if errorlevel 1 (
    echo [ERROR] Win32 SCS position plugin configuration failed.
    exit /b 1
)
echo [2/5] Building the Win32 SCS position plugin...
cmake --build "%X86_PLUGIN_BUILD_DIR%" --config Release --target nlsi
if errorlevel 1 (
    echo [ERROR] Win32 SCS position plugin build failed.
    exit /b 1
)
if not exist "%PLUGIN_RELEASE_DIR%\win_x64\nlsi.dll" (
    echo [ERROR] The x64 SCS position plugin was not created.
    exit /b 1
)
if not exist "%PLUGIN_RELEASE_DIR%\win_x86\nlsi.dll" (
    echo [ERROR] The x86 SCS position plugin was not created.
    exit /b 1
)

echo [3/5] Running Python compatibility and packaging tests...
py -3 -m unittest discover -s test -v
if errorlevel 1 (
    echo [ERROR] Python tests failed.
    exit /b 1
)

echo [4/5] Running native Release CTest...
ctest --test-dir "%BUILD_DIR%" -C Release --output-on-failure
if errorlevel 1 (
    echo [ERROR] Native Release CTest failed.
    exit /b 1
)

echo [5/5] Building the native installer...
call "%CD%\build-installer.bat"
if errorlevel 1 (
    echo [ERROR] Native installer build failed.
    exit /b 1
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
