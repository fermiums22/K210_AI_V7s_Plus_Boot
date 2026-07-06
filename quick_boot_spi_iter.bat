@echo off
setlocal EnableExtensions

set "PORT=%~1"
if "%PORT%"=="" set "PORT=COM12"
set "MONITOR_SECONDS=%~2"
if "%MONITOR_SECONDS%"=="" set "MONITOR_SECONDS=45"

cd /d "%~dp0" || exit /b 1
set "BOOT_REPO=%CD%"
for /f "delims=" %%B in ('git branch --show-current') do set "BOOT_BRANCH=%%B"
if "%BOOT_BRANCH%"=="" (
  echo ERROR: boot repo is not on a branch
  exit /b 10
)

echo === QUICK K210 BOOT SPI FAST ITERATION ===
echo Port: %PORT%
echo Boot repo: %BOOT_REPO%
echo Boot branch: %BOOT_BRANCH%
echo Monitor seconds: %MONITOR_SECONDS%
echo.

echo [1/3] Fetch branch, no SDK sync, no CMake configure
git fetch origin %BOOT_BRANCH% || exit /b 1
git switch -f %BOOT_BRANCH% || exit /b 1
git reset --hard origin/%BOOT_BRANCH% || exit /b 1

echo.
echo [2/3] Fast incremental build + flash boot
call build_boot_fast.bat || exit /b 2
call flash_boot.bat %PORT% --no-build --no-monitor --baud 1500000 || exit /b 2

echo.
echo [3/3] Monitor boot/app
call monitor_boot.bat %PORT% 115200 %MONITOR_SECONDS%
exit /b %ERRORLEVEL%
