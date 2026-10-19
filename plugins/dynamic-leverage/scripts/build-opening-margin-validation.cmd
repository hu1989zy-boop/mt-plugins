@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-opening-margin-validation.ps1" %*
exit /b %errorlevel%
