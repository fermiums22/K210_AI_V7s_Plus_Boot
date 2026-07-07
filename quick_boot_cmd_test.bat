@echo off
setlocal EnableExtensions

set "PORT=%~1"
if "%PORT%"=="" set "PORT=COM8"
set "APP_REPO=%~2"
if "%APP_REPO%"=="" set "APP_REPO=D:\w_spase\AI\K210_AI_V7s_Plus"
set "FORCE_SYNC=%~3"

cd /d "%~dp0" || exit /b 1
set "BOOT_REPO=%CD%"

chcp 65001 >nul
set "PYTHONUTF8=1"
set "PYTHONIOENCODING=utf-8"
set "NO_COLOR=1"
set "CLICOLOR=0"
set "PY_COLORS=0"
set "TERM=dumb"

for /f "delims=" %%B in ('git branch --show-current') do set "BOOT_BRANCH=%%B"
if "%BOOT_BRANCH%"=="" (
  echo ERROR: boot repo is not on a branch
  exit /b 10
)

echo === QUICK K210 BOOT COMMAND TEST ===
echo Port: %PORT%
echo Boot repo: %BOOT_REPO%
echo Boot branch: %BOOT_BRANCH%
echo App repo for SDK sync only: %APP_REPO%
echo.

echo [1/4] SDK sync check, no app build/flash
if /I "%FORCE_SYNC%"=="--sync" goto do_sync
if not exist "lib\hal" goto do_sync
if not exist "third_party\fatfs" goto do_sync
if not exist "lds\kendryte.ld" goto do_sync
echo SDK_SYNC_SKIP already present. Use --sync as 3rd arg to force bootstrap.
goto sync_done

:do_sync
call bootstrap_from_app.bat "%APP_REPO%" || exit /b 1

:sync_done
echo.
echo [2/4] Restore safe source tree after experimental SD DMA patches
git checkout -- src\boot_cmd.c lib\bsp\device\spi.cpp lib\drivers\src\storage\sdcard.cpp 2>nul

echo.
echo [3/4] Build boot only, force boot command mode
set "BOOT_SLOT_PROBE=0"
call build_boot.bat || exit /b 2
set "BOOT_SLOT_PROBE="

echo.
echo [4/4] Flash boot only + run safe command smoke
call flash_boot.bat %PORT% --no-build --no-monitor --baud 1500000 || exit /b 3
py -3 tools\boot_cmd_smoke.py %PORT% 115200 45
exit /b %ERRORLEVEL%
