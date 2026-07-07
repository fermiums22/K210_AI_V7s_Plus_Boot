extern "C" void install_hal(void);
extern "C" void install_drivers(void);

extern "C" void boot_sdk_install_drivers_once(void)
{
    static int installed = 0;
    if (installed)
        return;
    installed = 1;

    /* Match the SDK runtime order instead of installing device objects by hand.
     * install_hal() creates the global PIC handle and DMA free semaphore used by
     * pic_set_irq_*() and dma_open_free().  install_drivers() installs normal
     * system devices such as SPI/GPIO that the SD/FatFs path opens by name.
     */
    install_hal();
    install_drivers();
}
