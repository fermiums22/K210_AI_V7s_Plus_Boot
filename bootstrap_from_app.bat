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
  echo syncing %%D ...
  robocopy "%SRC%\%%D" "%%D" /MIR /NFL /NDL /NJH /NJS /NP >nul
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

py -3 -c "from pathlib import Path; p=Path('lds/kendryte.ld'); s=p.read_text(); s=s.replace('PROVIDE( _heap_end = 0x800E0000 );\n  PROVIDE( _boot_stack_core1 = 0x800F7000 );\n  PROVIDE( _boot_stack_core0 = 0x800FF000 );\n  ASSERT(_end < 0x800E0000, \"boot image/data overlaps heap/stack reserve below APP_LOAD_ADDR\");', 'PROVIDE( _heap_end = 0x800E0000 );'); p.write_text(s)"
if errorlevel 1 (
  echo ERROR: boot linker post-fix failed
  exit /b 6
)

echo BOOT_LINKER_POSTFIX_OK heap_end=0x800E0000 no_assert=1
echo.
echo OK: SDK/libs and shared SD/log sources synced and thinned for boot.
