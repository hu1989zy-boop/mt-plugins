@echo off
setlocal
rem Run only this project's PowerShell script; inherit the VS developer environment.
rem The execution policy applies to the child process, not to system settings.
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-windows.ps1" %*
exit /b %ERRORLEVEL%
