@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "SDK=%K210_SDK%"
if "%SDK%"=="" if exist "C:\K210\sdk\kendryte-freertos-sdk-0.7.0" set "SDK=C:\K210\sdk\kendryte-freertos-sdk-0.7.0"
if "%SDK%"=="" set "SDK=%CD%"

set "TC=%K210_TC%"
if "%TC%"=="" if exist "C:\K210\toolchain\kendryte-toolchain\bin\riscv64-unknown-elf-gcc.exe" set "TC=C:\K210\toolchain\kendryte-toolchain\bin"
if "%TC%"=="" set "TC=C:\K210\toolchain\kendryte-toolchain\bin"

set "BUILD=%CD%\build"
set "MAKE=%TC%\mingw32-make.exe"
if not exist "%MAKE%" set "MAKE=C:\msys64\mingw64\bin\mingw32-make.exe"
set "BOOT_BUILD_COLOR=%BOOT_BUILD_COLOR%"
if "%BOOT_BUILD_COLOR%"=="" set "BOOT_BUILD_COLOR=1"

echo === K210 boot fast incremental build ===
echo Repo:  %CD%
echo MAKE:  %MAKE%
echo BUILD: %BUILD%
echo.

if not exist "%BUILD%\Makefile" (
  echo ERROR: %BUILD%\Makefile not found.
  echo Run quick_boot_spi_test.bat once to configure/sync, then use this fast path.
  exit /b 2
)

if "%BOOT_BUILD_COLOR%"=="1" (
  py -3 tools\run_color_build.py -- "%MAKE%" -C "%BUILD%" -j4 k210_boot
) else (
  "%MAKE%" -C "%BUILD%" -j4 k210_boot
)
if errorlevel 1 exit /b 1

if not exist "%BUILD%\k210_boot.bin" (
  echo ERROR: binary missing: %BUILD%\k210_boot.bin
  exit /b 1
)

echo.
echo OK: %BUILD%\k210_boot.bin
exit /b 0
