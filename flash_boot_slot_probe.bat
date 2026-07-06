@echo off
setlocal EnableExtensions
cd /d "%~dp0"

chcp 65001 >nul
set "PYTHONUTF8=1"
set "PYTHONIOENCODING=utf-8"

set "PORT=%~1"
if "%PORT%"=="" set "PORT=COM8"

echo === K210 boot SLOT-PROBE diagnostic flash ===
echo Port: %PORT%
echo.
echo WARNING: this build enables BOOT_ENABLE_SLOT_PROBE=1.
echo If SPI3 flash probing hangs before runtime, reflash normal boot with:
echo   flash_boot.bat %PORT% --no-monitor --baud 1500000
echo.

set "BOOT_SLOT_PROBE=1"
call flash_boot.bat %PORT% --baud 1500000 --monitor-baud 115200 --monitor-seconds 30
exit /b %ERRORLEVEL%
