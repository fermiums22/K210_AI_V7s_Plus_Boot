#include "boot_config.h"
#include "boot_decision.h"
#include "boot_flash.h"
#include <stdint.h>

extern void boot_irq_off(void);
extern void boot_jump_to_app(uintptr_t entry);
extern int boot_flash_load_app_image_on_safe_stack(const boot_app_header_t *hdr);
void boot_runtime_start(uint32_t reason);

#ifndef BOOT_ENABLE_SLOT_PROBE
#define BOOT_ENABLE_SLOT_PROBE 1
#endif

int main(void)
{
    boot_irq_off();

#if BOOT_ENABLE_SLOT_PROBE
    uint32_t reason = boot_decision_get_reason();

    if (reason == 0) {
        int load_rc;
        load_rc = boot_flash_load_app_image_on_safe_stack(&boot_decision_app_header);
        if (load_rc != 0) {
            reason |= BOOT_REASON_APP_LOAD_FAIL;
        } else {
            boot_jump_to_app(boot_decision_app_header.entry_addr);
            for (;;)
                ;
        }
    }
#else
    uint32_t reason = BOOT_REASON_APP_INVALID;
    boot_decision_app_header_read_rc = -1000;
#endif

    boot_runtime_start(reason);

    for (;;)
        ;
}
