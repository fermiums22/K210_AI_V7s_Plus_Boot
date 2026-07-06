#include <FreeRTOS.h>
#include <task.h>
#include <stdint.h>

#include "boot_config.h"
#include "boot_decision.h"
#include "log.h"

static uint32_t g_boot_reason;

static void boot_task(void *arg)
{
    (void)arg;
    uint32_t n = 0;
    LOG("BOOT_TASK_START");
    for (;;) {
        LOGF("BOOT_ALIVE %lu reason=0x%08lx", (unsigned long)n++, (unsigned long)g_boot_reason);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void boot_runtime_start(uint32_t reason)
{
    g_boot_reason = reason;

    log_init();
    LOG("BOOT_MODE_ENTER");
    LOG("[boot] " BOOT_VERSION " start");
    LOGF("[boot] reset_status_raw=0x%08lx", (unsigned long)boot_decision_reset_status_raw);
    LOGF("[boot] reason=0x%08lx app_request=%u wdg=%u app_invalid=%u",
         (unsigned long)g_boot_reason,
         (unsigned)((g_boot_reason & BOOT_REASON_APP_REQUEST) != 0),
         (unsigned)((g_boot_reason & BOOT_REASON_WDG_RESET) != 0),
         (unsigned)((g_boot_reason & BOOT_REASON_APP_INVALID) != 0));

    if (xTaskCreate(boot_task, "boot", 4096, NULL, tskIDLE_PRIORITY + 2, NULL) != pdPASS) {
        for (;;) {
            LOG("BOOT_TASK_CREATE_FAIL");
            for (volatile uint32_t i = 0; i < 20000000u; ++i)
                __asm__ volatile("nop");
        }
    }

    vTaskStartScheduler();

    for (;;) {
        LOG("BOOT_SCHEDULER_RETURNED");
        for (volatile uint32_t i = 0; i < 20000000u; ++i)
            __asm__ volatile("nop");
    }
}
