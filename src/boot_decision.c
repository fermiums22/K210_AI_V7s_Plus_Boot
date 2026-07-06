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
    const boot_app_header_t *h = (const boot_app_header_t *)APP_LOAD_ADDR;
    if (h->magic != APP_HDR_MAGIC || h->magic_inv != APP_HDR_MAGIC_INV)
        return 0;
    if (h->load_addr != APP_LOAD_ADDR)
        return 0;
    if (h->entry_addr < APP_LOAD_ADDR || h->entry_addr >= BOOT_RAM_END)
        return 0;
    if (h->image_size == 0 || h->image_size > APP_MAX_SIZE)
        return 0;
    if ((h->load_addr + h->image_size) > BOOT_RAM_END)
        return 0;
    return 1;
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
