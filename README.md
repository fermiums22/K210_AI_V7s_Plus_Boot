# K210_AI_V7s_Plus_Boot

Minimal K210 updater/chainloader for `K210_AI_V7s_Plus`.

## Goal

The robot should not need to be opened just to update firmware.

Default boot flow:

```text
K210 ROM -> bootloader -> application
```

Bootloader stays almost invisible. It only stays in boot/update mode when:

1. application requested update mode;
2. previous application boot was not confirmed / watchdog recovery path;
3. application slot is empty/invalid;
4. SD update manifest is present and valid.

## K210-specific note

K210 ROM loads the firmware image into SRAM at `0x80000000`. It is not an STM32-style XIP boot. Therefore the bootloader cannot safely overwrite `0x80000000` with the app while it is still executing from there.

Baseline design:

```text
0x80000000  bootloader RAM image, loaded by ROM
0x80100000  application RAM image, loaded/jumped by bootloader
```

The application must be linked for `APP_LOAD_ADDR = 0x80100000` when it is booted by this bootloader. Its normal `_start` then configures its own trap/vector state.

## Current test separation

There are now two separate flows. Do not use the app-jump test when the goal is to test bootloader commands.

### 1. Bootloader command/runtime test

This is the main test for bootloader work. It does **not** build or flash the app. It forces `BOOT_SLOT_PROBE=0`, starts boot mode, then automatically tests:

- UART command loop;
- SD mount + 64-byte write/read verify;
- SPI3 JEDEC ID;
- SPI3 flash read;
- SPI3 flash erase/page-program/readback verify at scratch offset `0x00F00000`.

Run as one command:

```bat
cd /d D:\w_spase\AI\K210_AI_V7s_Plus_Boot && git switch main && git pull && quick_boot_cmd_test.bat COM8 ..\K210_AI_V7s_Plus
```

Expected final marker:

```text
BOOT_CMD_SMOKE_PASS
```

Expected command markers:

```text
KBOOT:HELLO
KBOOT:SD_OK rw-64
KBOOT:SPI3_ID 0xef6018
KBOOT:SPI3_READ_OK
KBOOT:SPI3_RW_OK offset=0x00f00000 size=256
```

If SDK/libs are missing or need to be refreshed from the app repo, force sync with:

```bat
quick_boot_cmd_test.bat COM8 ..\K210_AI_V7s_Plus --sync
```

### 2. App-jump test

This is only for debugging chainload into the app slot. It builds/flashes the app slot and then builds/flashes the bootloader, so it is intentionally heavier.

```bat
cd /d D:\w_spase\AI\K210_AI_V7s_Plus_Boot && git switch main && git pull && quick_app_boot_test.bat COM8 ..\K210_AI_V7s_Plus
```

## Manifest

Bootloader looks for:

```text
/fs/0/update/pending.json
```

Initial manifest shape:

```json
{
  "schema": 1,
  "job_id": "test-app-update",
  "once": true,
  "actions": [
    {
      "target": "k210-app",
      "op": "stage-app",
      "file": "k210_app_80100000.bin",
      "load_addr": "0x80100000",
      "entry": "0x80100000",
      "sha256": "optional-later"
    }
  ]
}
```

Current first milestone is intentionally small:

1. build bootloader;
2. mount SD;
3. print boot reason / manifest status on UART;
4. accept bootloader commands for SD/SPI3 tests;
5. jump to a test app linked at `0x80100000` when app-jump path is selected.

Flash-writing/A-B persistence is the next milestone after boot command path is stable.

## Local bootstrap

This repo intentionally reuses the known-working SDK/libs from sibling app repo.

Expected folder layout:

```text
D:\w_spase\AI\K210_AI_V7s_Plus
D:\w_spase\AI\K210_AI_V7s_Plus_Boot
```

Manual setup:

```bat
cd /d D:\w_spase\AI\K210_AI_V7s_Plus_Boot
bootstrap_from_app.bat ..\K210_AI_V7s_Plus
build_boot.bat
flash_boot.bat COM8 --no-build
```

## Legacy boot SPI/load test

`quick_boot_spi_test.bat` is retained for low-level boot SPI read/load diagnostics. It is not the primary command-mode test.

```bat
cd /d D:\w_spase\AI\K210_AI_V7s_Plus_Boot && git switch main && git pull && quick_boot_spi_test.bat COM8 ..\K210_AI_V7s_Plus
```
