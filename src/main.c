#include <FreeRTOS.h>
#include <task.h>
#include <devices.h>
#include <filesystem.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "boot_config.h"
#include "log.h"
#include "sd.h"

extern void boot_jump_to_app(uintptr_t entry);

static void stay_in_boot(const char *reason)
{
    uint32_t n = 0;
    LOGF("[boot] staying in boot: %s", reason ? reason : "unknown");
    for (;;) {
        LOGF("BOOT_ALIVE %lu %s", (unsigned long)n++, reason ? reason : "unknown");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static int file_exists(const char *path)
{
    handle_t f = filesystem_file_open(path, FILE_ACCESS_READ, FILE_MODE_OPEN_EXISTING);
    if (!f)
        return 0;
    filesystem_file_close(f);
    return 1;
}

static int load_file_to_ram(const char *path, uintptr_t addr, uint32_t max_size)
{
    handle_t f = filesystem_file_open(path, FILE_ACCESS_READ, FILE_MODE_OPEN_EXISTING);
    if (!f) {
        LOGF("[boot] app image missing: %s", path);
        return -1;
    }

    uint64_t size64 = filesystem_file_get_size(f);
    if (size64 == 0 || size64 > max_size) {
        LOGF("[boot] bad app image size: %lu", (unsigned long)size64);
        filesystem_file_close(f);
        return -2;
    }

    uint8_t *dst = (uint8_t *)addr;
    uint32_t left = (uint32_t)size64;
    uint32_t pos = 0;
    while (left) {
        uint32_t chunk = left > 4096u ? 4096u : left;
        int got = filesystem_file_read(f, dst + pos, chunk);
        if (got != (int)chunk) {
            LOGF("[boot] read failed at %lu", (unsigned long)pos);
            filesystem_file_close(f);
            return -3;
        }
        pos += chunk;
        left -= chunk;
    }
    filesystem_file_close(f);
    LOGF("[boot] loaded %s -> 0x%08lx size=%lu", path, (unsigned long)addr, (unsigned long)size64);
    return (int)size64;
}

static int manifest_present(void)
{
    return file_exists(BOOT_MANIFEST_PATH);
}

static void boot_task(void *arg)
{
    (void)arg;

    LOG("BOOT_TASK_START");
    LOG("[boot] " BOOT_VERSION " start");
    LOGF("[boot] default app load=0x%08lx entry=0x%08lx", (unsigned long)APP_LOAD_ADDR,
         (unsigned long)APP_ENTRY_ADDR);

    if (!sd_mount()) {
        LOG("[boot] SD mount failed");
        stay_in_boot("sd_mount_failed");
    }

    LOG("[boot] SD mounted");

    if (manifest_present()) {
        LOGF("[boot] update manifest present: %s", BOOT_MANIFEST_PATH);
        LOG("[boot] manifest parser/updater not enabled in milestone 0");
    } else {
        LOG("[boot] no update manifest");
    }

    if (!file_exists(BOOT_APP_IMAGE_PATH)) {
        LOGF("[boot] no app image at %s", BOOT_APP_IMAGE_PATH);
        stay_in_boot("no_app_image");
    }

    int size = load_file_to_ram(BOOT_APP_IMAGE_PATH, APP_LOAD_ADDR, APP_MAX_SIZE);
    if (size <= 0) {
        LOG("[boot] app load failed");
        stay_in_boot("app_load_failed");
    }

    LOG("[boot] jumping to app");
    vTaskDelay(pdMS_TO_TICKS(50));
    boot_jump_to_app(APP_ENTRY_ADDR);

    stay_in_boot("jump_returned");
}

int main(void)
{
    log_init();
    LOG("BOOT_EARLY_MAIN");
    LOG("BOOT_BEFORE_SCHEDULER");

    if (xTaskCreate(boot_task, "boot", 8192, NULL, tskIDLE_PRIORITY + 2, NULL) != pdPASS) {
        for (;;) {
            LOG("BOOT_TASK_CREATE_FAIL");
            for (volatile uint32_t i = 0; i < 20000000u; ++i) {
                __asm__ volatile("nop");
            }
        }
    }

    vTaskStartScheduler();

    for (;;) {
        LOG("BOOT_SCHEDULER_RETURNED");
        for (volatile uint32_t i = 0; i < 20000000u; ++i) {
            __asm__ volatile("nop");
        }
    }
}
