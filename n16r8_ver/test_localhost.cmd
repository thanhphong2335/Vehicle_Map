@echo off
setlocal
chcp 65001 >nul

echo ==========================================================
echo       MOTOMAP - TEST LOCALHOST WEB & API RUNNER
echo ==========================================================

set "ROOT_DIR=%~dp0.."
set "DATA_DIR=%~dp0data"
set "TOOLS_DIR=%~dp0tools"
set "ENV_FILE=%ROOT_DIR%\.note\doc_env"

REM 1. Kiem tra file cau hinh .note\doc_env
if not exist "%ENV_FILE%" (
    echo [CANH BAO] Khong tim thay file cau hinh: .note\doc_env
    echo He thong se tao file mau tai: %ENV_FILE%
    if not exist "%ROOT_DIR%\.note" mkdir "%ROOT_DIR%\.note"
    (
        echo # MOTOMAP API CONFIGURATION
        echo MAPBOX_API_KEY=
        echo LOCATIONIQ_API_KEY=
        echo DEFAULT_MAP_LAT=10.880539600217121
        echo DEFAULT_MAP_LON=106.80541333694616
    ) > "%ENV_FILE%"
    echo Da tao file .note\doc_env mau. Vui long mo file nay va dien API Key neu co!
    echo.
)

REM 2. Tu dong dong bo ENV sang data/env.js
echo [1/3] Dong bo cau hinh tu .note\doc_env sang data\env.js...
powershell -ExecutionPolicy Bypass -File "%TOOLS_DIR%\sync_env.ps1"
if %ERRORLEVEL% neq 0 (
    echo [LOI] Khong the dong bo env.js!
    pause
    exit /b 1
)
echo.

REM 3. Tu dong mo trinh duyet sau 2 giay (chay ngam)
echo [2/3] Dang chuan bi mo trinh duyet http://localhost:8080 ...
start "" powershell -NoProfile -Command "Start-Sleep -Seconds 2; Start-Process 'http://localhost:8080'"

REM 4. Khoi chay Web Server Localhost
echo [3/3] Dang khoi chay Local Web Server tai cong 8080...
echo Nhan Ctrl + C de dung server bat cu luc nao.
echo ----------------------------------------------------------
cd /d "%DATA_DIR%"

REM Uu tien dung npx serve, neu khong co thi fallback sang python
where npx >nul 2>nul
if %ERRORLEVEL% equ 0 (
    npx --yes serve . -p 8080
    exit /b %ERRORLEVEL%
)

where python >nul 2>nul
if %ERRORLEVEL% equ 0 (
    python -m http.server 8080
    exit /b %ERRORLEVEL%
)

echo [LOI] May tinh cua ban chua cai dat Node.js (npx) hoac Python!
echo Vui long cai dat Node.js tu https://nodejs.org/ de tiep tuc.
pause
exit /b 1

