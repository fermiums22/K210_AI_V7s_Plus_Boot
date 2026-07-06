#pragma once

#include <stdint.h>

#define BOOT_VERSION              "k210-boot-0.1"

/* K210 ROM loads this boot image at 0x80000000.  App must be linked away from
 * boot RAM image so boot can chainload without self-overwrite. */
#define BOOT_LOAD_ADDR            0x80000000u
#define APP_LOAD_ADDR             0x80100000u
#define APP_ENTRY_ADDR            0x80100000u
#define APP_MAX_SIZE              (4u * 1024u * 1024u)

#define BOOT_MANIFEST_PATH        "/fs/0/update/pending.json"
#define BOOT_APP_IMAGE_PATH       "/fs/0/update/app/k210_app_80100000.bin"

#define BOOT_FORCE_PATH           "/fs/0/update/force_boot.txt"
#define BOOT_STATE_PATH           "/fs/0/update/boot_state.json"
