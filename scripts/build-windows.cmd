@echo off
setlocal
rem Compatibility entry point; arguments and exit status pass to trade-copier.
call "%~dp0..\plugins\trade-copier\scripts\build-windows.cmd" %*
exit /b %ERRORLEVEL%
