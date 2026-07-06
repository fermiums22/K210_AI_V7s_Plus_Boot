@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "PORT=%~1"
if "%PORT%"=="" set "PORT=COM8"

echo === K210 boot RECOVERY flash ===
echo Port: %PORT%
echo.
echo This build disables app slot probing and always enters BOOT_MODE_ENTER.
echo Use it when a bad app slot makes normal boot silent/hang before app.
echo.

set "BOOT_SLOT_PROBE=0"
call flash_boot.bat %PORT% --no-monitor --baud 1500000
if errorlevel 1 exit /b 1

echo.
echo === K210 boot RECOVERY monitor ===
call monitor_boot.bat %PORT% 115200 30
exit /b %ERRORLEVEL%
