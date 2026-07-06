#include "boot_config.h"
#include "boot_flash.h"
#include "log.h"
#include <stdint.h>
#include <sysctl.h>

uint32_t boot_decision_reset_status_raw;
boot_app_header_t boot_decision_app_header;
int boot_decision_app_header_read_rc;

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

static int boot_decision_header_struct_valid(const boot_app_header_t *h)
{
    if (h->magic != APP_HDR_MAGIC || h->magic_inv != APP_HDR_MAGIC_INV)
        return 0;
    if (h->load_addr != APP_LOAD_ADDR)
        return 0;
    if (h->entry_addr < APP_LOAD_ADDR || h->entry_addr >= BOOT_RAM_END)
        return 0;
    if (h->image_size < sizeof(boot_app_header_t) || h->image_size > APP_SLOT0_MAX_SIZE)
        return 0;
    if ((h->load_addr + h->image_size) > BOOT_RAM_END)
        return 0;
    return 1;
}

int boot_decision_app_valid(void)
{
    LOG("BOOT_APP_HDR_READ_BEGIN");
    boot_decision_app_header_read_rc = boot_flash_read_app_header(APP_SLOT0_FLASH_OFFSET, &boot_decision_app_header);
    LOGF("BOOT_APP_HDR_READ_END rc=%d magic=0x%08lx", boot_decision_app_header_read_rc, (unsigned long)boot_decision_app_header.magic);
    if (boot_decision_app_header_read_rc != 0)
        return 0;
    return boot_decision_header_struct_valid(&boot_decision_app_header);
}

uint32_t boot_decision_get_reason(void)
{
    uint32_t reason = 0;
    int app_request;
    int wdg_reset;
    int app_valid;

    boot_decision_reset_status_raw = boot_decision_read_reset_status();

    app_request = boot_decision_app_request();
    wdg_reset = boot_decision_watchdog(boot_decision_reset_status_raw);
    app_valid = boot_decision_app_valid();

    if (app_request || wdg_reset || !app_valid) {
        if (app_request)
            reason |= BOOT_REASON_APP_REQUEST;
        if (wdg_reset)
            reason |= BOOT_REASON_WDG_RESET;
        if (!app_valid)
            reason |= BOOT_REASON_APP_INVALID;
    }

    return reason;
}
