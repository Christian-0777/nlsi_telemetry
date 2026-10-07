@echo off
setlocal
set "ROOT=%~dp0"
py -3 "%ROOT%installer\build_installer.py" %*
exit /b %ERRORLEVEL%
