#include <uarths.h>

extern void install_drivers();

extern "C" void boot_sdk_driver_install(void)
{
    uarths_puts("KBOOT:DRIVERS_INSTALL_BEGIN\n");
    install_drivers();
    uarths_puts("KBOOT:DRIVERS_INSTALL_OK\n");
}
