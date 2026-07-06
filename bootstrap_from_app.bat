@echo off
setlocal EnableExtensions

set "SRC=%~1"
if "%SRC%"=="" set "SRC=..\K210_AI_V7s_Plus"
for %%I in ("%SRC%") do set "SRC=%%~fI"
cd /d "%~dp0"

echo === K210 boot bootstrap ===
echo Source app repo: %SRC%
echo Boot repo:       %CD%
echo.

if not exist "%SRC%\CMakeLists.txt" (
  echo ERROR: app repo not found: %SRC%
  exit /b 2
)

for %%D in (cmake lib third_party lds) do (
  if exist "%%D" rmdir /s /q "%%D"
  echo copying %%D ...
  robocopy "%SRC%\%%D" "%%D" /E /NFL /NDL /NJH /NJS /NP >nul
  if errorlevel 8 exit /b 3
)

if not exist src mkdir src
copy /Y "%SRC%\src\sd.c" "src\sd.c" >nul
copy /Y "%SRC%\src\sd.h" "src\sd.h" >nul
copy /Y "%SRC%\src\pinout.h" "src\pinout.h" >nul
copy /Y "%SRC%\src\log.c" "src\log.c" >nul
copy /Y "%SRC%\src\log.h" "src\log.h" >nul

py -3 -c "from pathlib import Path; p=Path('src/log.h'); s=p.read_text(); s=s.replace('#define APP_LOG_BAUD 921600u', '#define APP_LOG_BAUD 115200u'); p.write_text(s)"
if errorlevel 1 (
  echo ERROR: boot log baud patch failed
  exit /b 4
)

py -3 tools\write_thin_cmake.py
if errorlevel 1 (
  echo ERROR: thin boot CMake patch failed
  exit /b 5
)

echo.
echo OK: SDK/libs and shared SD/log sources copied and thinned for boot.
