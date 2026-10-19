@echo off
setlocal
where py >nul 2>&1
if errorlevel 1 goto python
py -3 -B "%~dp0server.py" %*
goto finish
:python
python -B "%~dp0server.py" %*
:finish
if errorlevel 1 pause
endlocal
