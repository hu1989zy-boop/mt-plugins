@echo off
setlocal
rem Compatibility entry point; arguments and exit status pass to MT5.
call "%~dp0..\mt5\scripts\build-windows.cmd" %*
exit /b %ERRORLEVEL%
