#include "boot_config.h"
#include "boot_decision.h"
#include "boot_flash.h"
#include <stdint.h>

extern void boot_irq_off(void);
extern void boot_jump_to_app(uintptr_t entry);
void boot_runtime_start(uint32_t reason);

int main(void)
{
    boot_irq_off();

    uint32_t reason = boot_decision_get_reason();
    if (reason == 0) {
        if (boot_flash_load_app_image(&boot_decision_app_header) != 0)
            reason |= BOOT_REASON_APP_LOAD_FAIL;
        else {
            boot_jump_to_app(boot_decision_app_header.entry_addr);
            for (;;)
                ;
        }
    }

    boot_runtime_start(reason);

    for (;;)
        ;
}
