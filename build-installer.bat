@echo off
setlocal
cd /d "%~dp0"
if errorlevel 1 exit /b 1

set "PYTHON=%~dp0.venv\Scripts\python.exe"
if not exist "%PYTHON%" set "PYTHON=py -3"

if exist "%~dp0.venv\Scripts\python.exe" (
    "%PYTHON%" "%~dp0installer\build_native_installer.py"
) else (
    py -3 "%~dp0installer\build_native_installer.py"
)
exit /b %ERRORLEVEL%
