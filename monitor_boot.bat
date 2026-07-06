@echo off
setlocal EnableExtensions
chcp 65001 >nul
set "PYTHONUTF8=1"
set "PYTHONIOENCODING=utf-8"

set "PORT=%~1"
if "%PORT%"=="" set "PORT=COM8"
set "BAUD=%~2"
if "%BAUD%"=="" set "BAUD=921600"
set "SECONDS=%~3"
if "%SECONDS%"=="" set "SECONDS=15"

echo === K210 boot monitor ===
echo Port: %PORT%
echo Baud: %BAUD%
echo Time: %SECONDS%s
echo.

py -3 tools\boot_monitor.py --port %PORT% --baud %BAUD% --seconds %SECONDS%
exit /b %ERRORLEVEL%
