@echo off
setlocal
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Collect-MT5Logs.ps1" -Mode Stop
set "MT5_CAPTURE_EXIT=%ERRORLEVEL%"
echo.
pause
endlocal & exit /b %MT5_CAPTURE_EXIT%
