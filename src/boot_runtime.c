#include <FreeRTOS.h>
#include <task.h>
#include <stdint.h>
#include <clint.h>
#include <encoding.h>

#include "boot_cmd.h"
#include "log.h"

extern void boot_sdk_install_drivers_once(void);

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
    LOG("BOOT_TASK_START");
    boot_sdk_install_drivers_once();
    boot_cmd_service_run(g_boot_reason);
    for (;;)
        vTaskDelay(pdMS_TO_TICKS(1000));
}

static void boot_halt_no_scheduler(const char *msg)
{
    LOG(msg);
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
    for (;;)
        vTaskDelay(pdMS_TO_TICKS(1000));
}

void boot_runtime_start(uint32_t reason)
{
    g_boot_reason = reason;

    log_init();
    LOG("BOOT_MODE_ENTER");
    LOGF("BOOT_MODE_REASON 0x%08lx", (unsigned long)g_boot_reason);

    boot_prepare_freertos_runtime();

    if (xTaskCreate(boot_task, "bootcmd", 8192, NULL, tskIDLE_PRIORITY + 2, NULL) != pdPASS)
        boot_halt_no_scheduler("BOOT_TASK_CREATE_FAIL");

    vTaskStartScheduler();
    boot_halt_no_scheduler("BOOT_SCHEDULER_RETURNED");
}
