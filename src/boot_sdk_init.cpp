#include <cstddef>
#include <uarths.h>
#include <devices.h>
#include <kernel/driver.hpp>
#include <string.h>

static int install_named_driver(const char *name)
{
    sys::driver_registry_t *head = sys::g_system_drivers;
    while (head->name) {
        if (strcmp(head->name, name) == 0) {
            head->driver_ptr->install();
            return 0;
        }
        head++;
    }
    return -1;
}

extern "C" int boot_sdk_driver_install(void)
{
    uarths_puts("KBOOT:DRIVERS_INSTALL_BEGIN\n");
    if (install_named_driver("/dev/spi1") != 0) {
        uarths_puts("KBOOT:DRIVERS_INSTALL_FAIL install\n");
        return -1;
    }

    handle_t spi = io_open("/dev/spi1");
    handle_t gpio = io_open("/dev/gpio0");
    if (!spi || !gpio) {
        if (spi)
            io_close(spi);
        if (gpio)
            io_close(gpio);
        uarths_puts("KBOOT:DRIVERS_INSTALL_FAIL spi1/gpio0\n");
        return -1;
    }

    io_close(gpio);
    io_close(spi);
    uarths_puts("KBOOT:DRIVERS_INSTALL_OK\n");
    return 0;
}
