#include <FreeRTOS.h>
#include <task.h>
#include <stdint.h>
#include <clint.h>
#include <encoding.h>

#include "boot_config.h"
#include "boot_decision.h"
#include "boot_flash.h"
#include "log.h"

static uint32_t g_boot_reason;
static StaticTask_t s_idle_task;
static StackType_t s_idle_task_stack[configMINIMAL_STACK_SIZE];

static void boot_prepare_freertos_runtime(void)
{
    clear_csr(mie, MIP_MTIP);
    clint_ipi_enable();
    set_csr(mstatus, MSTATUS_MIE);
}

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

static void boot_halt_no_scheduler(const char *msg)
{
    LOG(msg);
    for (;;)
        __asm__ volatile("wfi");
}

void vApplicationIdleHook(void)
{
}

void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t **ppxIdleTaskStackBuffer,
                                   uint32_t *pulIdleTaskStackSize)
{
    *ppxIdleTaskTCBBuffer = &s_idle_task;
    *ppxIdleTaskStackBuffer = s_idle_task_stack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    LOGF("BOOT_STACK_OVERFLOW %s", pcTaskName ? pcTaskName : "unknown");
    for (;;)
        __asm__ volatile("wfi");
}

void boot_runtime_start(uint32_t reason)
{
    g_boot_reason = reason;

    log_init();
    LOG("BOOT_MODE_ENTER");
    LOG("[boot] " BOOT_VERSION " start");
    LOGF("[boot] reset_status_raw=0x%08lx", (unsigned long)boot_decision_reset_status_raw);
    LOGF("[boot] reason=0x%08lx app_request=%u wdg=%u app_invalid=%u app_load_fail=%u",
         (unsigned long)g_boot_reason,
         (unsigned)((g_boot_reason & BOOT_REASON_APP_REQUEST) != 0),
         (unsigned)((g_boot_reason & BOOT_REASON_WDG_RESET) != 0),
         (unsigned)((g_boot_reason & BOOT_REASON_APP_INVALID) != 0),
         (unsigned)((g_boot_reason & BOOT_REASON_APP_LOAD_FAIL) != 0));
    LOGF("[boot] flash_jedec_id=0x%06lx", (unsigned long)boot_flash_read_jedec_id());
    LOGF("[boot] app_hdr magic=0x%08lx load=0x%08lx entry=0x%08lx size=%lu",
         (unsigned long)boot_decision_app_header.magic,
         (unsigned long)boot_decision_app_header.load_addr,
         (unsigned long)boot_decision_app_header.entry_addr,
         (unsigned long)boot_decision_app_header.image_size);

    boot_prepare_freertos_runtime();

    if (xTaskCreate(boot_task, "boot", 4096, NULL, tskIDLE_PRIORITY + 2, NULL) != pdPASS)
        boot_halt_no_scheduler("BOOT_TASK_CREATE_FAIL");

    vTaskStartScheduler();

    boot_halt_no_scheduler("BOOT_SCHEDULER_RETURNED");
}
