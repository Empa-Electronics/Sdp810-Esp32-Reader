@echo off
rem Starts the live plotter. Pass plotter options through, for example: run.bat -p COM5 -w 60
setlocal
set "DIR=%~dp0"
if not exist "%DIR%.venv\Scripts\python.exe" (
    echo The Python environment is missing. Run install.bat in the main folder first.
    pause
    exit /b 1
)
"%DIR%.venv\Scripts\python.exe" "%DIR%sdp_plotter.py" %*
if errorlevel 1 pause
