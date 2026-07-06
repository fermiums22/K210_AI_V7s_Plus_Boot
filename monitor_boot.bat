@echo off
setlocal EnableExtensions
cd /d "%~dp0"

chcp 65001 >nul
set "PYTHONUTF8=1"
set "PYTHONIOENCODING=utf-8"

set "PORT=%~1"
if "%PORT%"=="" set "PORT=COM8"
set "BAUD=%~2"
if "%BAUD%"=="" set "BAUD=115200"
set "SECONDS=%~3"
if "%SECONDS%"=="" set "SECONDS=15"

echo === K210 boot/app monitor ===
echo Repo: %CD%
echo Port: %PORT%
echo Baud: %BAUD%
echo Time: %SECONDS%s
echo.

if not exist "%CD%\tools\boot_monitor.py" (
  echo ERROR: monitor script not found: %CD%\tools\boot_monitor.py
  exit /b 1
)

py -3 "%CD%\tools\boot_monitor.py" --port %PORT% --baud %BAUD% --seconds %SECONDS%
exit /b %ERRORLEVEL%
