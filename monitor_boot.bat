@echo off
setlocal EnableExtensions
chcp 65001 >nul
set "PYTHONUTF8=1"
set "PYTHONIOENCODING=utf-8"

set "PORT=%~1"
if "%PORT%"=="" set "PORT=COM8"
set "BAUD=%~2"
if "%BAUD%"=="" set "BAUD=921600"

echo === K210 boot monitor ===
echo Port: %PORT%
echo Baud: %BAUD%
echo Ctrl+C to stop.
echo.

py -3 -c "import sys, serial; p=sys.argv[1]; b=int(sys.argv[2]); s=serial.Serial(p,b,timeout=None); print('monitor open',p,b); [print(s.readline().decode('utf-8','replace').rstrip()) for _ in iter(int,1)]" %PORT% %BAUD%
