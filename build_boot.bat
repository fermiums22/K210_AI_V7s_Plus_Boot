@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "TC=%K210_TC%"
if "%TC%"=="" set "TC=C:\K210\toolchain\kendryte-toolchain\bin"
set "BUILD=%CD%\build"
set "NINJA=C:\msys64\mingw64\bin\ninja.exe"

if not exist "%NINJA%" (
  echo ERROR: ninja not found: %NINJA%
  exit /b 2
)

cmake -S firmware_v2 -B "%BUILD%" -G Ninja -DCMAKE_MAKE_PROGRAM="%NINJA%" -DTOOLCHAIN="%TC%"
if errorlevel 1 exit /b 1
cmake --build "%BUILD%" --parallel
if errorlevel 1 exit /b 1

if not exist "%BUILD%\k210_boot_v2.bin" exit /b 1
echo K210_BOOT_BUILD_OK %BUILD%\k210_boot_v2.bin
