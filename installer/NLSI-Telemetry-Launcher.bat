@echo off
setlocal

set "APP_ROOT=C:\nlsi-tem"
if not exist "%APP_ROOT%\app\agent.py" (
    echo NLSI Telemetry is not installed at "%APP_ROOT%".
    echo Expected: "%APP_ROOT%\app\agent.py"
    exit /b 1
)

set "PYTHON_EXE=%APP_ROOT%\runtime\python.exe"
if not exist "%PYTHON_EXE%" (
    echo Missing bundled NLSI Python runtime.
    echo Expected runtime: "%PYTHON_EXE%"
    echo Repair the installation or reinstall NLSI Telemetry.
    exit /b 1
)

pushd "%APP_ROOT%\app"
"%PYTHON_EXE%" agent.py
exit /b %ERRORLEVEL%
