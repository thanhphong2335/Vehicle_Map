@echo off
setlocal

set "PIO=%USERPROFILE%\.platformio\penv\Scripts\platformio.exe"
if not exist "%PIO%" set "PIO=%USERPROFILE%\.platformio\penv\Scripts\pio.exe"

if not exist "%PIO%" (
  echo PlatformIO Core not found.
  echo Install the PlatformIO IDE extension in VS Code first.
  exit /b 1
)

if /I "%~1"=="build" goto build
if /I "%~1"=="upload" goto upload
if /I "%~1"=="uploadfs" goto uploadfs
if /I "%~1"=="monitor" goto monitor

echo Usage:
echo   build.cmd build
echo   build.cmd upload
echo   build.cmd uploadfs
echo   build.cmd monitor
exit /b 2

:build
"%PIO%" run
exit /b %ERRORLEVEL%

:upload
"%PIO%" run --target upload
exit /b %ERRORLEVEL%

:uploadfs
"%PIO%" run --target uploadfs
exit /b %ERRORLEVEL%

:monitor
"%PIO%" device monitor
exit /b %ERRORLEVEL%
