@echo off
setlocal EnableExtensions

set "PORT=%~1"
if "%PORT%"=="" set "PORT=COM8"
set "APP_REPO=%~2"
if "%APP_REPO%"=="" set "APP_REPO=D:\w_spase\AI\K210_AI_V7s_Plus"

cd /d "%~dp0" || exit /b 1
set "BOOT_REPO=%CD%"
for /f "delims=" %%B in ('git branch --show-current') do set "BOOT_BRANCH=%%B"
if "%BOOT_BRANCH%"=="" (
  echo ERROR: boot repo is not on a branch
  exit /b 10
)

echo === QUICK K210 APP SLOT0 TEST ===
echo Port: %PORT%
echo Boot repo: %BOOT_REPO%
echo Boot branch: %BOOT_BRANCH%
echo App repo: %APP_REPO%
echo.

echo [1/5] App incremental build
cd /d "%APP_REPO%" || exit /b 1
git fetch origin main || exit /b 1
git switch -f main || exit /b 1
git reset --hard origin/main || exit /b 1
call build_k210_app_slot0.bat || exit /b 1

echo.
echo [2/5] Flash app slot0
call flash_k210_app_slot0.bat %PORT% --no-build --baud 1500000 --monitor-baud 115200 --monitor-seconds 1 || exit /b 2

echo.
echo [3/5] Boot incremental sync
cd /d "%BOOT_REPO%" || exit /b 3
git fetch origin %BOOT_BRANCH% || exit /b 3
git switch -f %BOOT_BRANCH% || exit /b 3
git reset --hard origin/%BOOT_BRANCH% || exit /b 3
call bootstrap_from_app.bat "%APP_REPO%" || exit /b 3

echo.
echo [4/5] Flash boot with slot probe ON
set "BOOT_SLOT_PROBE=1"
call flash_boot.bat %PORT% --no-monitor --baud 1500000 || exit /b 4
set "BOOT_SLOT_PROBE="

echo.
echo [5/5] Monitor
call monitor_boot.bat %PORT% 115200 45
exit /b %ERRORLEVEL%
