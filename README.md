# K210_AI_V7s_Plus_Boot

Minimal K210 updater/chainloader for `K210_AI_V7s_Plus`.

## Goal

The robot should not need to be opened just to update firmware.

Default boot flow:

```text
K210 ROM -> bootloader -> application
```

Bootloader stays almost invisible.  It only stays in boot/update mode when:

1. application requested update mode;
2. previous application boot was not confirmed / watchdog recovery path;
3. application slot is empty/invalid;
4. SD update manifest is present and valid.

## K210-specific note

K210 ROM loads the firmware image into SRAM at `0x80000000`.  It is not an STM32-style XIP boot.  Therefore the bootloader cannot safely overwrite `0x80000000` with the app while it is still executing from there.

Baseline design:

```text
0x80000000  bootloader RAM image, loaded by ROM
0x80100000  application RAM image, loaded/jumped by bootloader
```

The application must be linked for `APP_LOAD_ADDR = 0x80100000` when it is booted by this bootloader.  Its normal `_start` then configures its own trap/vector state.

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
4. jump to a test app linked at `0x80100000` if present.

Flash-writing/A-B persistence is the next milestone after chainload works.

## Local bootstrap

This repo intentionally reuses the known-working SDK/libs from sibling app repo.

Expected folder layout:

```text
D:\w_spase\AI\K210_AI_V7s_Plus
D:\w_spase\AI\K210_AI_V7s_Plus_Boot
```

Run:

```bat
cd /d D:\w_spase\AI\K210_AI_V7s_Plus_Boot
bootstrap_from_app.bat ..\K210_AI_V7s_Plus
build_boot.bat
flash_boot.bat COM8 --no-build
```
