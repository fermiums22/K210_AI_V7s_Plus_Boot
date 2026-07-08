#include <FreeRTOS.h>
#include <task.h>
#include <stdint.h>
#include <clint.h>
#include <encoding.h>
#include <uarths.h>

#include "boot_cmd.h"
#include "log.h"

extern void boot_sdk_driver_install(void);

static uint32_t g_boot_reason;
static StaticTask_t s_idle_task;
static StackType_t s_idle_task_stack[configMINIMAL_STACK_SIZE];

static void boot_prepare_freertos_runtime(void)
{
    /* Do not set global MIE here.  FreeRTOS enables interrupts when the
     * scheduler is ready.  Enabling MIE before vTaskStartScheduler can leave
     * the K210 in a silent trap/hang before the boot command task starts. */
    clear_csr(mie, MIP_MTIP);
    clint_ipi_enable();
}

static void boot_task(void *arg)
{
    (void)arg;
    uarths_puts("KBOOT:TASK_START\n");
    LOG("BOOT_TASK_START");
    boot_cmd_service_run(g_boot_reason);
    for (;;)
        vTaskDelay(pdMS_TO_TICKS(1000));
}

static void boot_halt_no_scheduler(const char *msg)
{
    LOG(msg);
    uarths_puts("KBOOT:HALT_NO_SCHEDULER\n");
    for (;;)
        vTaskDelay(pdMS_TO_TICKS(1000));
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
    uarths_puts("KBOOT:STACK_OVERFLOW\n");
    for (;;)
        vTaskDelay(pdMS_TO_TICKS(1000));
}

void boot_runtime_start(uint32_t reason)
{
    g_boot_reason = reason;

    log_init();
    uarths_puts("KBOOT:RUNTIME_START\n");
    LOG("BOOT_MODE_ENTER");
    LOGF("BOOT_MODE_REASON 0x%08lx", (unsigned long)g_boot_reason);

    boot_sdk_driver_install();
    boot_prepare_freertos_runtime();

    if (xTaskCreate(boot_task, "bootcmd", 8192, NULL, tskIDLE_PRIORITY + 2, NULL) != pdPASS)
        boot_halt_no_scheduler("BOOT_TASK_CREATE_FAIL");

    uarths_puts("KBOOT:SCHED_START\n");
    vTaskStartScheduler();
    boot_halt_no_scheduler("BOOT_SCHEDULER_RETURNED");
}
