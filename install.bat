@echo off
rem Installs the SDP810 reader: flashes the firmware to an ESP32-C3 and sets up the PC plotter.
rem Usage: install.bat            (finds the board automatically)
rem        install.bat COM5       (use this port)
setlocal EnableExtensions
cd /d "%~dp0"
title SDP810 reader installer

echo ============================================
echo   SDP810 reader installer (ESP32-C3)
echo ============================================
echo.

rem ---- 1. Python -------------------------------------------------------------
set "PY="
where py >nul 2>nul && set "PY=py -3"
if not defined PY where python >nul 2>nul && set "PY=python"
if not defined PY goto :no_python
%PY% -c "import sys; sys.exit(0 if sys.version_info >= (3, 9) else 1)" >nul 2>nul
if errorlevel 1 goto :no_python
for /f "usebackq delims=" %%v in (`%PY% -c "import platform; print(platform.python_version())"`) do echo [1/4] Python %%v found

rem ---- 2. Virtual environment and packages ------------------------------------
rem Paths are relative: the script has already changed to its own folder, and a
rem folder name with brackets, like "sdp810_reader (1)", would break the ( ) blocks below.
set "VPY=tools\.venv\Scripts\python.exe"
if not exist "%VPY%" (
    echo [2/4] Creating Python environment in tools\.venv ...
    %PY% -m venv tools\.venv
    if errorlevel 1 goto :fail
) else (
    echo [2/4] Python environment already exists
)
echo       Installing packages: pyserial, numpy, matplotlib, esptool (first time takes a minute) ...
"%VPY%" -m pip install --disable-pip-version-check -q -r tools\requirements.txt
if errorlevel 1 goto :fail

rem ---- 3. Serial port ---------------------------------------------------------
set "PORT=%~1"
if defined PORT goto :have_port
echo [3/4] Looking for the ESP32-C3 ...
for /f "usebackq delims=" %%p in (`"%VPY%" tools\find_port.py`) do set "PORT=%%p"
if defined PORT goto :have_port
echo.
echo       No USB serial port was found automatically.
echo       Check the USB cable (it must carry data) and the CP210x driver:
echo       https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers
echo.
set /p "PORT=      Type the COM port (for example COM5), or press Enter to quit: "
if not defined PORT goto :fail
:have_port
echo       Using port %PORT%

rem ---- 4. Flash ---------------------------------------------------------------
echo [4/4] Flashing firmware\sdp810_reader_esp32c3.bin ...
echo       Close any serial monitor or plotter that is using %PORT% first.
"%VPY%" -m esptool --chip esp32c3 --port %PORT% --baud 460800 --before default_reset --after hard_reset write_flash 0x0 firmware\sdp810_reader_esp32c3.bin
if errorlevel 1 goto :flash_fail

echo.
echo ============================================
echo   Done. The board is running the firmware.
echo   Start the plotter any time with: tools\run.bat
echo ============================================
echo.
choice /c YN /m "Start the live plotter now"
if errorlevel 2 goto :end
call tools\run.bat -p %PORT%
goto :end

:no_python
echo.
echo Python 3.9 or newer was not found.
echo Install it from https://www.python.org/downloads/ and tick
echo "Add python.exe to PATH" in the installer, then run install.bat again.
echo (The "python" shortcut that opens the Microsoft Store does not count.)
goto :fail

:flash_fail
echo.
echo Flashing failed. Things to try:
echo   - close other programs using %PORT% (serial monitor, plotter, Arduino IDE)
echo   - hold the BOOT button, press and release RESET, release BOOT, then run again
echo   - try another USB cable or port
goto :fail

:fail
echo.
echo Installation did not finish.
pause
exit /b 1

:end
endlocal
exit /b 0
