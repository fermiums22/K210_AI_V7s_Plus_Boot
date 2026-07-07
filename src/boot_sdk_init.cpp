#include "log.h"

void install_drivers();

extern "C" void boot_sdk_install_drivers_once(void)
{
    static int installed = 0;
    if (installed)
        return;
    installed = 1;
    LOG("BOOT_SDK_INSTALL_DRIVERS");
    install_drivers();
    LOG("BOOT_SDK_INSTALL_DRIVERS_DONE");
}
