#pragma once

#include <stdint.h>

#define BOOT_VERSION              "k210-boot-0.2"

#define BOOT_LOAD_ADDR            0x80000000u
#define APP_LOAD_ADDR             0x80100000u
#define APP_ENTRY_ADDR            0x80100000u
#define APP_MAX_SIZE              (4u * 1024u * 1024u)
#define BOOT_RAM_END              0x80600000u

#define BOOT_REQ_ADDR             0x805FF000u
#define BOOT_REQ_MAGIC            0x424F4F54u
#define BOOT_REQ_MAGIC_INV        0xBDB0B0ABu

#define APP_HDR_MAGIC             0x4B323130u
#define APP_HDR_MAGIC_INV         0xB4CDCEDFu

#define BOOT_REASON_APP_REQUEST   (1u << 0)
#define BOOT_REASON_WDG_RESET     (1u << 1)
#define BOOT_REASON_APP_INVALID   (1u << 2)

#define BOOT_MANIFEST_PATH        "/fs/0/update/pending.json"
#define BOOT_APP_IMAGE_PATH       "/fs/0/update/app/k210_app_80100000.bin"

#define BOOT_FORCE_PATH           "/fs/0/update/force_boot.txt"
#define BOOT_STATE_PATH           "/fs/0/update/boot_state.json"

typedef struct boot_app_header
{
    uint32_t magic;
    uint32_t magic_inv;
    uint32_t load_addr;
    uint32_t entry_addr;
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t flags;
    uint32_t reserved;
} boot_app_header_t;

typedef struct boot_request_mailbox
{
    uint32_t magic;
    uint32_t magic_inv;
    uint32_t command;
    uint32_t arg;
} boot_request_mailbox_t;
