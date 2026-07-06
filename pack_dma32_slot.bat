@echo off
setlocal

if "%~1"=="" (
  echo Usage: %~nx0 input_slot0.bin [output_slot0_dma32.bin]
  exit /b 2
)

set "IN=%~1"
set "OUT=%~2"

if not exist "%IN%" (
  echo ERROR: input not found: %IN%
  exit /b 1
)

if "%OUT%"=="" set "OUT=%~dpn1.dma32%~x1"

python "%~dp0tools\pack_dma32_slot.py" "%IN%" "%OUT%"
exit /b %ERRORLEVEL%
