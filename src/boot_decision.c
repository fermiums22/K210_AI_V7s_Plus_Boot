#include "boot_config.h"
#include <stdint.h>
#include <sysctl.h>

uint32_t boot_decision_reset_status_raw;

uint32_t boot_decision_read_reset_status(void)
{
    return *((volatile uint32_t *)&sysctl->reset_status);
}

int boot_decision_watchdog(uint32_t reset_status_raw)
{
    return (reset_status_raw & ((1u << 2) | (1u << 3))) != 0;
}

int boot_decision_app_request(void)
{
    const volatile boot_request_mailbox_t *mb = (const volatile boot_request_mailbox_t *)BOOT_REQ_ADDR;
    return (mb->magic == BOOT_REQ_MAGIC && mb->magic_inv == BOOT_REQ_MAGIC_INV);
}

int boot_decision_app_valid(void)
{
    /* Do not trust SRAM at APP_LOAD_ADDR after reset.  SRAM can contain stale
     * data from the previous run or from the ISP stub.  Until the bootloader
     * can read and verify a persistent flash slot header, the application is
     * intentionally considered invalid and we stay in boot mode.
     */
    return 0;
}

uint32_t boot_decision_get_reason(void)
{
    uint32_t reason = 0;
    boot_decision_reset_status_raw = boot_decision_read_reset_status();
    if (boot_decision_app_request())
        reason |= BOOT_REASON_APP_REQUEST;
    if (boot_decision_watchdog(boot_decision_reset_status_raw))
        reason |= BOOT_REASON_WDG_RESET;
    if (!boot_decision_app_valid())
        reason |= BOOT_REASON_APP_INVALID;
    return reason;
}
