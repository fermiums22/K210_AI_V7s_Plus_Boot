@echo off
setlocal EnableExtensions
cd /d "%~dp0"

chcp 65001 >nul
set "PYTHONUTF8=1"
set "PYTHONIOENCODING=utf-8"

set "PORT=%~1"
if "%PORT%"=="" set "PORT=COM8"
set "NO_BUILD=0"
set "NO_MONITOR=0"
set "MONITOR_SECONDS=15"
set "KFLASH_BAUD=1500000"
shift /1

:parse_args
if "%~1"=="" goto args_done
if /I "%~1"=="--no-build" (
  set "NO_BUILD=1"
  shift /1
  goto parse_args
)
if /I "%~1"=="--no-monitor" (
  set "NO_MONITOR=1"
  shift /1
  goto parse_args
)
if /I "%~1"=="--monitor-seconds" (
  set "MONITOR_SECONDS=%~2"
  shift /1
  shift /1
  goto parse_args
)
if /I "%~1"=="--baud" (
  set "KFLASH_BAUD=%~2"
  shift /1
  shift /1
  goto parse_args
)
echo ERROR: unknown argument: %~1
exit /b 2
:args_done

set "BIN=%CD%\build\k210_boot.bin"

echo === K210 boot flash ===
echo Repo: %CD%
echo Port: %PORT%
echo Baud: %KFLASH_BAUD%
echo.

reg add HKCU\Console /v VirtualTerminalLevel /t REG_DWORD /d 1 /f >nul 2>nul
py -3 -c "import ctypes; k=ctypes.windll.kernel32; ENABLE=4; handles=(-11,-12); mode=ctypes.c_uint(); [k.SetConsoleMode(k.GetStdHandle(h), (k.GetConsoleMode(k.GetStdHandle(h), ctypes.byref(mode)) and (mode.value|ENABLE)) or (mode.value|ENABLE)) for h in handles]" >nul 2>nul

if "%NO_BUILD%"=="0" (
  call build_boot.bat
  if errorlevel 1 exit /b 1
) else (
  if not exist "%BIN%" (
    echo ERROR: %BIN% not found. Run build_boot.bat first.
    exit /b 1
  )
)

py -3 -m kflash --help >nul 2>nul
if errorlevel 1 (
  echo ERROR: kflash is not installed for Windows Python.
  echo Run: py -3 -m pip install kflash
  exit /b 1
)

py -3 -m kflash -p %PORT% -b %KFLASH_BAUD% -B dan "%BIN%"
if errorlevel 1 exit /b 1

echo.
echo OK: K210 boot flashed.

if "%NO_MONITOR%"=="0" (
  echo.
  echo === K210 boot auto-monitor ===
  call monitor_boot.bat %PORT% 921600 %MONITOR_SECONDS%
)

exit /b 0
