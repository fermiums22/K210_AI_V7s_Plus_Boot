#include "boot_config.h"
#include "boot_decision.h"
#include <stdint.h>

extern void boot_irq_off(void);
extern void boot_jump_to_app(uintptr_t entry);
void boot_runtime_start(uint32_t reason);

int main(void)
{
    boot_irq_off();

    uint32_t reason = boot_decision_get_reason();
    if (reason == 0) {
        const boot_app_header_t *app = (const boot_app_header_t *)APP_LOAD_ADDR;
        boot_jump_to_app(app->entry_addr);
        for (;;)
            ;
    }

    boot_runtime_start(reason);

    for (;;)
        ;
}
