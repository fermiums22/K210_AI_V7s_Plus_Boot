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

set "KFLASH_BAUD=1500000"
set "APP_MONITOR_BAUD=921600"
set "APP_MONITOR_SECONDS=45"

echo === K210 slot0 app smoke test ===
echo App repo:   %APP_REPO%
echo Port:       %PORT%
echo Flash baud: %KFLASH_BAUD%
echo App UART:   %APP_MONITOR_BAUD%
echo.
echo NOTE: bootloader is not rebuilt/flashed here. This test only builds/flashes slot0 app.
echo.

if not exist "%APP_REPO%\.git" (
  echo ERROR: app repo not found: %APP_REPO%
  exit /b 2
)

pushd "%APP_REPO%"
if errorlevel 1 exit /b 1

echo [app] sync main...
git fetch origin main
if errorlevel 1 goto fail_popd
git switch -f main
if errorlevel 1 goto fail_popd
git reset --hard origin/main
if errorlevel 1 goto fail_popd

echo.
echo [app] clean slot0 build...
if exist build_app_slot0 rmdir /s /q build_app_slot0
if exist build_app_slot0 (
  echo ERROR: failed to remove build_app_slot0
  goto fail_popd
)

echo.
echo [app] build slot0 image...
call build_k210_app_slot0.bat
if errorlevel 1 goto fail_popd

echo.
echo [app] flash slot0 at 0x00100000 and monitor app UART...
call flash_k210_app_slot0.bat %PORT% --no-build --baud %KFLASH_BAUD% --monitor-baud %APP_MONITOR_BAUD% --monitor-seconds %APP_MONITOR_SECONDS%
if errorlevel 1 goto fail_popd

popd
echo.
echo OK: slot0 app smoke test finished.
echo EXPECTED: boot silently jumps to app, then app/KSD logs at %APP_MONITOR_BAUD%.
exit /b 0

:fail_popd
popd
exit /b 1
