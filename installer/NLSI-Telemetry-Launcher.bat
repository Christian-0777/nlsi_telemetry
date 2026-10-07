@echo off
setlocal

set "APP_ROOT=%~dp0"
if "%APP_ROOT:~-1%"=="\" set "APP_ROOT=%APP_ROOT:~0,-1%"
if not exist "%APP_ROOT%\app\agent.py" (
    echo NLSI Telemetry is not installed correctly.
    echo Expected: "%APP_ROOT%\app\agent.py"
    exit /b 1
)

set "PYTHON_EXE=%APP_ROOT%\runtime\python.exe"
pushd "%APP_ROOT%\app"
if exist "%PYTHON_EXE%" goto bundled_runtime

where.exe py.exe >nul 2>nul
if errorlevel 1 goto missing_python

py -3 agent.py
set "EXIT_CODE=%ERRORLEVEL%"
popd
exit /b %EXIT_CODE%

:bundled_runtime
"%PYTHON_EXE%" agent.py
set "EXIT_CODE=%ERRORLEVEL%"
popd
exit /b %EXIT_CODE%

:missing_python
popd
echo Python 3.10 or newer is required to run NLSI Telemetry.
echo Install Python with the Windows Python Launcher, or repair the bundled runtime.
exit /b 1
exit /b %ERRORLEVEL%
