@echo off
setlocal EnableExtensions
cd /d "%~dp0"

chcp 65001 >nul
set "PYTHONUTF8=1"
set "PYTHONIOENCODING=utf-8"

set "PORT=%~1"
if "%PORT%"=="" set "PORT=COM8"

set "APP_REPO_ARG=%~2"
if "%APP_REPO_ARG%"=="" set "APP_REPO_ARG=..\K210_AI_V7s_Plus"
for %%I in ("%APP_REPO_ARG%") do set "APP_REPO=%%~fI"

set "BOOT_REPO=%CD%"
set "KFLASH_BAUD=1500000"
set "APP_MONITOR_BAUD=921600"
set "APP_MONITOR_SECONDS=30"

echo === K210 boot + slot0 app smoke test ===
echo Boot repo: %BOOT_REPO%
echo App repo:  %APP_REPO%
echo Port:      %PORT%
echo Flash baud:%KFLASH_BAUD%
echo App UART:  %APP_MONITOR_BAUD%
echo.

if not exist "%APP_REPO%\.git" (
  echo ERROR: app repo not found: %APP_REPO%
  exit /b 2
)

if not exist "%BOOT_REPO%\.git" (
  echo ERROR: boot repo not found: %BOOT_REPO%
  exit /b 2
)

echo [boot] bootstrap SDK/libs from app repo...
call bootstrap_from_app.bat "%APP_REPO%"
if errorlevel 1 exit /b 1

echo.
echo [boot] clean build...
if exist "%BOOT_REPO%\build" rmdir /s /q "%BOOT_REPO%\build"
if exist "%BOOT_REPO%\build" (
  echo ERROR: failed to remove %BOOT_REPO%\build
  exit /b 1
)

echo.
echo [boot] build...
call build_boot.bat
if errorlevel 1 exit /b 1

echo.
echo [boot] flash to 0x00000000...
call flash_boot.bat %PORT% --no-build --no-monitor --baud %KFLASH_BAUD%
if errorlevel 1 exit /b 1

echo.
echo [app] sync main...
pushd "%APP_REPO%"
if errorlevel 1 exit /b 1
git fetch origin main
if errorlevel 1 goto app_fail_popd
git switch -f main
if errorlevel 1 goto app_fail_popd
git reset --hard origin/main
if errorlevel 1 goto app_fail_popd

echo.
echo [app] clean slot0 build...
if exist build_app_slot0 rmdir /s /q build_app_slot0
if exist build_app_slot0 (
  echo ERROR: failed to remove build_app_slot0
  goto app_fail_popd
)

echo.
echo [app] build slot0 image...
call build_k210_app_slot0.bat
if errorlevel 1 goto app_fail_popd

echo.
echo [app] flash slot0 at 0x00100000 and monitor app UART...
call flash_k210_app_slot0.bat %PORT% --no-build --baud %KFLASH_BAUD% --monitor-baud %APP_MONITOR_BAUD% --monitor-seconds %APP_MONITOR_SECONDS%
if errorlevel 1 goto app_fail_popd
popd

echo.
echo OK: slot0 app smoke test finished.
echo EXPECTED: no BOOT_MODE_ENTER after valid app header; app/KSD logs at %APP_MONITOR_BAUD%.
exit /b 0

:app_fail_popd
popd
exit /b 1
