@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "SDK=%K210_SDK%"
if "%SDK%"=="" if exist "C:\K210\sdk\kendryte-freertos-sdk-0.7.0" set "SDK=C:\K210\sdk\kendryte-freertos-sdk-0.7.0"
if "%SDK%"=="" set "SDK=%CD%"

set "TC=%K210_TC%"
if "%TC%"=="" if exist "C:\K210\toolchain\kendryte-toolchain\bin\riscv64-unknown-elf-gcc.exe" set "TC=C:\K210\toolchain\kendryte-toolchain\bin"
if "%TC%"=="" set "TC=C:\K210\toolchain\kendryte-toolchain\bin"

set "BOOT_SLOT_PROBE=%BOOT_SLOT_PROBE%"
if "%BOOT_SLOT_PROBE%"=="" set "BOOT_SLOT_PROBE=0"

set "BUILD=%CD%\build"
set "MAKE=%TC%\mingw32-make.exe"
if not exist "%MAKE%" set "MAKE=C:\msys64\mingw64\bin\mingw32-make.exe"

echo === K210 boot build ===
echo Repo:  %CD%
echo TC:    %TC%
echo MAKE:  %MAKE%
echo BUILD: %BUILD%
echo Slot probe: %BOOT_SLOT_PROBE%
echo.

if not exist "%TC%" (
  echo ERROR: K210 toolchain not found: %TC%
  exit /b 1
)
if not exist "%MAKE%" (
  echo ERROR: mingw32-make.exe not found
  exit /b 1
)
where cmake >nul 2>nul
if errorlevel 1 (
  echo ERROR: cmake not found in PATH
  exit /b 1
)
if not exist "lib\hal" (
  echo ERROR: SDK/lib folders missing. Run bootstrap_from_app.bat first.
  exit /b 2
)

if not exist "%BUILD%" mkdir "%BUILD%"

echo [cmake] configuring...
cmake -S . -B "%BUILD%" -G "MinGW Makefiles" -DCMAKE_MAKE_PROGRAM="%MAKE%" -DTOOLCHAIN="%TC%" -DSDK_ROOT="%SDK%" -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DBOOT_ENABLE_SLOT_PROBE=%BOOT_SLOT_PROBE%
if errorlevel 1 exit /b 1

echo [make] building...
"%MAKE%" -C "%BUILD%" -j4
if errorlevel 1 exit /b 1

if not exist "%BUILD%\k210_boot.bin" (
  echo ERROR: binary missing: %BUILD%\k210_boot.bin
  exit /b 1
)

echo.
echo OK: %BUILD%\k210_boot.bin
exit /b 0
