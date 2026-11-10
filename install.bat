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
rem Python 3.12.10 from python.org, used only when no Python 3.9+ is installed.
rem The SHA-256 values match the digests in python.org's signed .sigstore files.
set "PYVER=3.12.10"
set "PYARCH=amd64"
set "PYHASH=67b5635e80ea51072b87941312d00ec8927c4db9ba18938f7ad2d27b328b95fb"
set "PYDIR=%LOCALAPPDATA%\Programs\Python\Python312"
if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64" set "PYARCH=arm64"
if /i "%PROCESSOR_ARCHITEW6432%"=="ARM64" set "PYARCH=arm64"
if "%PYARCH%"=="arm64" set "PYHASH=377ac8fd478987940088e879441e702a71b53164d2a1e6f1d51ff77a7e470258"
if "%PYARCH%"=="arm64" set "PYDIR=%LOCALAPPDATA%\Programs\Python\Python312-arm64"

set "PY="
call :try_python py -3
if not defined PY call :try_python python
if not defined PY call :try_python "%PYDIR%\python.exe"
if not defined PY goto :offer_python
:python_ok
%PY% -c "import platform; print('[1/4] Python ' + platform.python_version() + ' found')"

rem ---- 2. Virtual environment and packages ------------------------------------
rem Paths are relative: the script has already changed to its own folder.
set "VPY=tools\.venv\Scripts\python.exe"
if exist "%VPY%" goto :have_venv
echo [2/4] Creating Python environment in tools\.venv ...
%PY% -m venv tools\.venv
if errorlevel 1 goto :fail
goto :venv_ready
:have_venv
echo [2/4] Python environment already exists
:venv_ready
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

:offer_python
echo [1/4] Python 3.9 or newer was not found.
if /i "%PROCESSOR_ARCHITECTURE%"=="x86" if not defined PROCESSOR_ARCHITEW6432 goto :no_python
choice /c YN /m "      Download and install Python %PYVER% from python.org now (about 27 MB)"
if errorlevel 2 goto :no_python
set "PYSETUP=%TEMP%\python-%PYVER%-%PYARCH%.exe"
set "PYURL=https://www.python.org/ftp/python/%PYVER%/python-%PYVER%-%PYARCH%.exe"
if exist "%PYSETUP%" del "%PYSETUP%"
echo       Downloading %PYURL% ...
curl.exe -L --fail --silent --show-error -o "%PYSETUP%" "%PYURL%"
if errorlevel 1 powershell -NoProfile -ExecutionPolicy Bypass -Command "Invoke-WebRequest -UseBasicParsing -Uri $env:PYURL -OutFile $env:PYSETUP"
if not exist "%PYSETUP%" goto :py_download_fail
certutil -hashfile "%PYSETUP%" SHA256 | findstr /i /x "%PYHASH%" >nul
if errorlevel 1 goto :py_hash_fail
echo       Installing Python for this user only (no administrator rights needed) ...
"%PYSETUP%" /quiet InstallAllUsers=0 PrependPath=1 Include_launcher=0 Include_test=0 TargetDir="%PYDIR%"
if errorlevel 1 goto :py_install_fail
del "%PYSETUP%"
call :try_python "%PYDIR%\python.exe"
if not defined PY goto :py_install_fail
goto :python_ok

:py_download_fail
echo.
echo Could not download Python. Check the internet connection, or install it yourself.
goto :no_python

:py_hash_fail
del "%PYSETUP%"
echo.
echo The downloaded Python installer did not match the expected checksum and was deleted.
goto :no_python

:py_install_fail
echo.
echo The Python installer did not finish.
goto :no_python

:no_python
echo.
echo Python 3.9 or newer is required. Install it from https://www.python.org/downloads/
echo and tick "Add python.exe to PATH", then run install.bat again.
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

rem Sets PY to the given command if it runs Python 3.9 or newer.
:try_python
%* -c "import sys; sys.exit(0 if sys.version_info >= (3, 9) else 1)" >nul 2>nul
if not errorlevel 1 set "PY=%*"
exit /b 0
