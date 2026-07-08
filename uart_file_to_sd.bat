@echo off
setlocal EnableExtensions

set "PORT=%~1"
set "LOCAL_FILE=%~2"
set "SD_PATH=%~3"
set "FLASH_OFF=%~4"
set "KFLASH_BAUD=%~5"

if "%PORT%"=="" set "PORT=COM8"
if "%LOCAL_FILE%"=="" (
  echo Usage: uart_file_to_sd.bat [PORT] LOCAL_FILE SD_PATH [FLASH_OFF] [KFLASH_BAUD]
  echo Example: uart_file_to_sd.bat COM8 build\app.bin 0:/app_slot0.bin 0x00F00000 1500000
  exit /b 2
)
if "%SD_PATH%"=="" set "SD_PATH=0:/app_slot0.bin"
if "%FLASH_OFF%"=="" set "FLASH_OFF=0x00100000"
if "%KFLASH_BAUD%"=="" set "KFLASH_BAUD=1500000"

cd /d "%~dp0" || exit /b 1

if not exist "%LOCAL_FILE%" (
  echo ERROR: local file not found: %LOCAL_FILE%
  exit /b 2
)

for %%F in ("%LOCAL_FILE%") do set "FILE_SIZE=%%~zF"
if "%FILE_SIZE%"=="0" (
  echo ERROR: empty local file: %LOCAL_FILE%
  exit /b 2
)

echo === UART FILE TO SD VIA SPI3 SCRATCH ===
echo Port: %PORT%
echo Local file: %LOCAL_FILE%
echo Size: %FILE_SIZE%
echo Scratch offset: %FLASH_OFF%
echo SD path: %SD_PATH%
echo.

echo [1/4] flash forced boot command mode
set "BOOT_SLOT_PROBE=0"
call build_boot.bat || exit /b 3
set "BOOT_SLOT_PROBE="
call flash_boot.bat %PORT% --no-build --no-monitor --baud %KFLASH_BAUD% || exit /b 3

echo.
echo [2/4] kflash local file to SPI3 scratch
set "KFPKG=%TEMP%\k210_uart_file_to_sd.kfpkg"
py -3 tools\make_raw_kfpkg.py "%LOCAL_FILE%" %FLASH_OFF% "%KFPKG%"
if errorlevel 1 exit /b 4
py -3 -m kflash -p %PORT% -b %KFLASH_BAUD% -B dan --noansi "%KFPKG%"
if errorlevel 1 exit /b 4

echo.
echo [3/4] boot copies SPI3 scratch to SD
set "BOOT_SERIAL_RESET=0"
py -3 -u tools\boot_cmd_sd_debug.py %PORT% 115200 240 "FLASH_SD %FLASH_OFF% %FILE_SIZE% %SD_PATH%"
if errorlevel 1 exit /b 5

echo.
echo [4/4] restore normal boot
call build_boot.bat || exit /b 6
call flash_boot.bat %PORT% --no-build --no-monitor --baud %KFLASH_BAUD% || exit /b 6

echo.
echo UART_FILE_TO_SD_PASS
exit /b 0
