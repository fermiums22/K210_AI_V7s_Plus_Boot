@echo off
setlocal EnableExtensions

set "PORT=%~1"
if "%PORT%"=="" set "PORT=COM12"
set "APP_REPO=%~2"
if "%APP_REPO%"=="" set "APP_REPO=D:\w_space\K210_AI_V7s_Plus"

cd /d "%~dp0" || exit /b 1
set "BOOT_REPO=%CD%"
for /f "delims=" %%B in ('git branch --show-current') do set "BOOT_BRANCH=%%B"
if "%BOOT_BRANCH%"=="" (
  echo ERROR: boot repo is not on a branch
  exit /b 10
)

echo === QUICK K210 BOOT SPI TEST ===
echo Port: %PORT%
echo Boot repo: %BOOT_REPO%
echo Boot branch: %BOOT_BRANCH%
echo App repo for SDK sync only: %APP_REPO%
echo.

echo [1/3] Boot incremental sync only, no app build/flash
cd /d "%BOOT_REPO%" || exit /b 1
git fetch origin %BOOT_BRANCH% || exit /b 1
git switch -f %BOOT_BRANCH% || exit /b 1
git reset --hard origin/%BOOT_BRANCH% || exit /b 1
call bootstrap_from_app.bat "%APP_REPO%" || exit /b 1

echo.
echo [2/3] Flash boot with slot probe ON
set "BOOT_SLOT_PROBE=1"
call flash_boot.bat %PORT% --no-monitor --baud 1500000 || exit /b 2
set "BOOT_SLOT_PROBE="

echo.
echo [3/3] Monitor boot/app
call monitor_boot.bat %PORT% 115200 45
exit /b %ERRORLEVEL%
