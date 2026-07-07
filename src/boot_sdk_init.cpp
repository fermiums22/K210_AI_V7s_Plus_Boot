#include <kernel/driver.hpp>

extern sys::driver &g_pic_driver_plic0;
extern sys::driver &g_dmac_driver_dmac0;
extern sys::driver &g_dma_driver_dma0;
extern sys::driver &g_dma_driver_dma1;
extern sys::driver &g_dma_driver_dma2;
extern sys::driver &g_dma_driver_dma3;
extern sys::driver &g_dma_driver_dma4;
extern sys::driver &g_dma_driver_dma5;
extern sys::driver &g_spi_driver_spi1;
extern sys::driver &g_gpiohs_driver_gpio0;

extern "C" void boot_sdk_install_drivers_once(void)
{
    static int installed = 0;
    if (installed)
        return;
    installed = 1;

    /* Minimal SDK runtime needed by boot SD/FatFs path:
     * - PLIC for DMA completion interrupts;
     * - DMAC core + channels for SPI DMA;
     * - SPI1 and GPIOHS for microSD over SPI1 with software CS.
     * Avoid full install_drivers() here to keep boot mode small and predictable.
     */
    g_pic_driver_plic0.install();
    g_dmac_driver_dmac0.install();
    g_dma_driver_dma0.install();
    g_dma_driver_dma1.install();
    g_dma_driver_dma2.install();
    g_dma_driver_dma3.install();
    g_dma_driver_dma4.install();
    g_dma_driver_dma5.install();
    g_spi_driver_spi1.install();
    g_gpiohs_driver_gpio0.install();
}
