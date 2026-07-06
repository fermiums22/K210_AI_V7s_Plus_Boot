#include "boot_config.h"
#include "boot_decision.h"
#include "boot_flash.h"
#include "log.h"
#include <stdint.h>

extern void boot_irq_off(void);
extern void boot_jump_to_app(uintptr_t entry);
void boot_runtime_start(uint32_t reason);

int main(void)
{
    boot_irq_off();

    /* Temporary early diagnostics: if flash probing hangs before boot runtime,
     * we still get a marker and know the USB-UART path is alive. Remove this
     * once SPI flash slot loading is stable. */
    log_init();
    LOG("BOOT_PRE_DECISION");

    uint32_t reason = boot_decision_get_reason();
    LOGF("BOOT_POST_DECISION reason=0x%08lx hdr_rc=%d magic=0x%08lx",
         (unsigned long)reason,
         boot_decision_app_header_read_rc,
         (unsigned long)boot_decision_app_header.magic);

    if (reason == 0) {
        int load_rc;
        LOGF("BOOT_LOAD_APP size=%lu entry=0x%08lx",
             (unsigned long)boot_decision_app_header.image_size,
             (unsigned long)boot_decision_app_header.entry_addr);
        load_rc = boot_flash_load_app_image(&boot_decision_app_header);
        if (load_rc != 0) {
            LOGF("BOOT_LOAD_FAIL rc=%d", load_rc);
            reason |= BOOT_REASON_APP_LOAD_FAIL;
        } else {
            LOGF("BOOT_JUMP_APP entry=0x%08lx", (unsigned long)boot_decision_app_header.entry_addr);
            boot_jump_to_app(boot_decision_app_header.entry_addr);
            for (;;)
                ;
        }
    }

    boot_runtime_start(reason);

    for (;;)
        ;
}
