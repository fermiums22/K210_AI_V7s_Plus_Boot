@echo off
setlocal EnableExtensions

set "PORT=%~1"
if "%PORT%"=="" set "PORT=COM12"
set "APP_REPO=%~2"
if "%APP_REPO%"=="" set "APP_REPO=D:\w_space\K210_AI_V7s_Plus"

cd /d "%~dp0"
set "BOOT_REPO=%CD%"

echo === FULL K210 RECOVERY + APP SLOT0 + NORMAL BOOT TEST ===
echo Port:     %PORT%
echo Boot repo:%BOOT_REPO%
echo App repo: %APP_REPO%
echo.

echo [1/7] Update boot repo
git fetch origin main || exit /b 1
git switch -f main || exit /b 1
git reset --hard origin/main || exit /b 1

echo.
echo [2/7] Bootstrap boot SDK from app repo
call bootstrap_from_app.bat "%APP_REPO%" || exit /b 2

echo.
echo [3/7] Flash recovery boot, slot probe OFF
set "BOOT_SLOT_PROBE=0"
call flash_boot.bat %PORT% --no-monitor --baud 1500000 || exit /b 3
set "BOOT_SLOT_PROBE="

echo.
echo [4/7] Recovery monitor auto-closes in 3s. Do NOT press Ctrl+C.
echo Expected: BOOT_SLOT_PROBE_DISABLED and BOOT_ALIVE.
call monitor_boot.bat %PORT% 115200 3

echo.
echo [5/7] Update app repo and clean-build slot0
cd /d "%APP_REPO%" || exit /b 5
git fetch origin main || exit /b 5
git switch -f main || exit /b 5
git reset --hard origin/main || exit /b 5
if exist build_app_slot0 (
  echo [clean] removing build_app_slot0 ...
  rmdir /s /q build_app_slot0
)
call build_k210_app_slot0.bat || exit /b 5

echo.
echo [6/7] Flash clean app slot0
call flash_k210_app_slot0.bat %PORT% --no-build --baud 1500000 --monitor-baud 115200 --monitor-seconds 1 || exit /b 6

echo.
echo [7/7] Flash normal boot, slot probe ON, final monitor
cd /d "%BOOT_REPO%" || exit /b 7
git fetch origin main || exit /b 7
git switch -f main || exit /b 7
git reset --hard origin/main || exit /b 7
call bootstrap_from_app.bat "%APP_REPO%" || exit /b 7
call flash_boot.bat %PORT% --no-monitor --baud 1500000 || exit /b 7

echo.
echo === FINAL MONITOR: expected BOOT_LOAD_DONE, BOOT_JUMP_APP, app alive ===
call monitor_boot.bat %PORT% 115200 45
exit /b %ERRORLEVEL%
