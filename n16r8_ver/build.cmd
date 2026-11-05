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
if /I "%~1"=="buildfs" goto buildfs
if /I "%~1"=="upload" goto upload
if /I "%~1"=="uploadfs" goto uploadfs
if /I "%~1"=="ota" goto ota
if /I "%~1"=="otafs" goto otafs
if /I "%~1"=="monitor" goto monitor

echo Usage:
echo   build.cmd build                (Compile firmware.bin)
echo   build.cmd buildfs              (Compile littlefs.bin)
echo   build.cmd upload               (Flash firmware via USB cable)
echo   build.cmd uploadfs             (Flash LittleFS via USB cable)
echo   build.cmd ota [IP]             (Flash firmware via Wi-Fi OTA, default 192.168.4.1)
echo   build.cmd otafs [IP]           (Flash LittleFS via Wi-Fi OTA, default 192.168.4.1)
echo   build.cmd monitor
exit /b 2

:build
"%PIO%" run -e esp32s3_n16r8
exit /b %ERRORLEVEL%

:buildfs
powershell -ExecutionPolicy Bypass -File "%~dp0tools\sync_env.ps1"
"%PIO%" run -e esp32s3_n16r8 --target buildfs
exit /b %ERRORLEVEL%

:upload
"%PIO%" run -e esp32s3_n16r8 --target upload
exit /b %ERRORLEVEL%

:uploadfs
powershell -ExecutionPolicy Bypass -File "%~dp0tools\sync_env.ps1"
"%PIO%" run -e esp32s3_n16r8 --target uploadfs
exit /b %ERRORLEVEL%

:ota
set "OTA_IP=%~2"
if "%OTA_IP%"=="" set "OTA_IP=192.168.4.1"
echo [OTA] Flashing firmware via Wi-Fi to %OTA_IP%...
"%PIO%" run -e esp32s3_n16r8_ota --upload-port %OTA_IP% --target upload
exit /b %ERRORLEVEL%

:otafs
set "OTA_IP=%~2"
if "%OTA_IP%"=="" set "OTA_IP=192.168.4.1"
echo [OTA] Flashing LittleFS via Wi-Fi to %OTA_IP%...
powershell -ExecutionPolicy Bypass -File "%~dp0tools\sync_env.ps1"
"%PIO%" run -e esp32s3_n16r8_ota --upload-port %OTA_IP% --target uploadfs
exit /b %ERRORLEVEL%

:monitor
"%PIO%" device monitor
exit /b %ERRORLEVEL%
