#include "boot_update.h"

#include "boot_config.h"
#include "boot_flash.h"
#include "log.h"
#include "sd.h"

#include <filesystem.h>
#include <ff.h>
#include <stdint.h>
#include <string.h>

#define BOOT_UPDATE_FS_PATH      "/fs/0/k210_app_slot0.bin"
#define BOOT_UPDATE_FAT_PATH     "0:/k210_app_slot0.bin"
#define BOOT_UPDATE_DONE_PATH    "0:/k210_app_slot0.done.bin"
#define BOOT_UPDATE_BUF_SIZE     (4u * 1024u)
#define BOOT_UPDATE_LOG_STEP     (256u * 1024u)

static uint8_t s_update_buf[BOOT_UPDATE_BUF_SIZE] __attribute__((aligned(64)));

static int header_sane_for_slot0(const boot_app_header_t *h, uint32_t file_size)
{
    if (!h)
        return 0;
    if (h->magic != APP_HDR_MAGIC || h->magic_inv != APP_HDR_MAGIC_INV)
        return 0;
    if (h->load_addr != APP_LOAD_ADDR)
        return 0;
    if (h->entry_addr < APP_LOAD_ADDR || h->entry_addr >= BOOT_RAM_END)
        return 0;
    if (h->image_size < sizeof(boot_app_header_t) || h->image_size > APP_SLOT0_MAX_SIZE)
        return 0;
    if (h->image_size != file_size)
        return 0;
    return 1;
}

static int verify_slot_header(uint32_t expected_size)
{
    boot_app_header_t h;
    int rc = boot_flash_read_app_header(APP_SLOT0_FLASH_OFFSET, &h);
    if (rc != 0) {
        LOGF("BOOT_SD_UPDATE_VERIFY_READ_FAIL rc=%d", rc);
        return -1;
    }
    if (!header_sane_for_slot0(&h, expected_size)) {
        LOGF("BOOT_SD_UPDATE_VERIFY_BAD magic=0x%08lx inv=0x%08lx load=0x%08lx entry=0x%08lx size=%lu expected=%lu",
             (unsigned long)h.magic,
             (unsigned long)h.magic_inv,
             (unsigned long)h.load_addr,
             (unsigned long)h.entry_addr,
             (unsigned long)h.image_size,
             (unsigned long)expected_size);
        return -2;
    }
    LOGF("BOOT_SD_UPDATE_VERIFY_OK size=%lu entry=0x%08lx",
         (unsigned long)h.image_size,
         (unsigned long)h.entry_addr);
    return 0;
}

int boot_update_try_sd_app_slot0(void)
{
    handle_t f;
    uint64_t size64;
    uint32_t size;
    uint32_t done = 0;
    uint32_t next_log = BOOT_UPDATE_LOG_STEP;
    boot_app_header_t first_hdr;

    LOG("BOOT_SD_UPDATE_CHECK " BOOT_UPDATE_FS_PATH);

    if (!sd_mount()) {
        LOG("BOOT_SD_UPDATE_SD_MOUNT_FAIL");
        return -1;
    }

    f = filesystem_file_open(BOOT_UPDATE_FS_PATH, FILE_ACCESS_READ, FILE_MODE_OPEN_EXISTING);
    if (!f) {
        LOG("BOOT_SD_UPDATE_NO_FILE");
        return 0;
    }

    size64 = filesystem_file_get_size(f);
    if (size64 == 0 || size64 > APP_SLOT0_MAX_SIZE || size64 > 0xffffffffu) {
        filesystem_file_close(f);
        LOGF("BOOT_SD_UPDATE_BAD_SIZE %lu", (unsigned long)size64);
        return -2;
    }
    size = (uint32_t)size64;

    int got = filesystem_file_read(f, s_update_buf, sizeof(first_hdr));
    if (got != (int)sizeof(first_hdr)) {
        filesystem_file_close(f);
        LOGF("BOOT_SD_UPDATE_HEADER_READ_FAIL got=%d", got);
        return -3;
    }
    memcpy(&first_hdr, s_update_buf, sizeof(first_hdr));
    if (!header_sane_for_slot0(&first_hdr, size)) {
        filesystem_file_close(f);
        LOGF("BOOT_SD_UPDATE_HEADER_BAD magic=0x%08lx inv=0x%08lx load=0x%08lx entry=0x%08lx size=%lu file=%lu",
             (unsigned long)first_hdr.magic,
             (unsigned long)first_hdr.magic_inv,
             (unsigned long)first_hdr.load_addr,
             (unsigned long)first_hdr.entry_addr,
             (unsigned long)first_hdr.image_size,
             (unsigned long)size);
        return -4;
    }

    filesystem_file_close(f);

    LOGF("BOOT_SD_UPDATE_BEGIN file=%lu slot=0x%08lx",
         (unsigned long)size,
         (unsigned long)APP_SLOT0_FLASH_OFFSET);

    int rc = boot_flash_erase_range(APP_SLOT0_FLASH_OFFSET, size);
    if (rc != 0) {
        LOGF("BOOT_SD_UPDATE_ERASE_FAIL rc=%d", rc);
        return -5;
    }
    LOG("BOOT_SD_UPDATE_ERASE_DONE");

    f = filesystem_file_open(BOOT_UPDATE_FS_PATH, FILE_ACCESS_READ, FILE_MODE_OPEN_EXISTING);
    if (!f) {
        LOG("BOOT_SD_UPDATE_REOPEN_FAIL");
        return -6;
    }

    while (done < size) {
        uint32_t want = size - done;
        if (want > sizeof(s_update_buf))
            want = sizeof(s_update_buf);
        got = filesystem_file_read(f, s_update_buf, want);
        if (got <= 0) {
            filesystem_file_close(f);
            LOGF("BOOT_SD_UPDATE_READ_FAIL done=%lu got=%d", (unsigned long)done, got);
            return -7;
        }

        rc = boot_flash_program(APP_SLOT0_FLASH_OFFSET + done, s_update_buf, (uint32_t)got);
        if (rc != 0) {
            filesystem_file_close(f);
            LOGF("BOOT_SD_UPDATE_PROGRAM_FAIL offset=0x%08lx rc=%d",
                 (unsigned long)(APP_SLOT0_FLASH_OFFSET + done),
                 rc);
            return -8;
        }

        done += (uint32_t)got;
        if (done >= next_log || done >= size) {
            LOGF("BOOT_SD_UPDATE_PROGRESS %lu/%lu",
                 (unsigned long)done,
                 (unsigned long)size);
            next_log += BOOT_UPDATE_LOG_STEP;
        }
    }

    filesystem_file_close(f);

    if (done != size) {
        LOGF("BOOT_SD_UPDATE_SHORT done=%lu size=%lu", (unsigned long)done, (unsigned long)size);
        return -9;
    }

    rc = verify_slot_header(size);
    if (rc != 0)
        return -10;

    f_unlink(BOOT_UPDATE_DONE_PATH);
    FRESULT fr = f_rename(BOOT_UPDATE_FAT_PATH, BOOT_UPDATE_DONE_PATH);
    LOGF("BOOT_SD_UPDATE_RENAME fr=%d", (int)fr);

    LOG("BOOT_SD_UPDATE_OK");
    return 1;
}
