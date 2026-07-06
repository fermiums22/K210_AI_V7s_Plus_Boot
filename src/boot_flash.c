#include "boot_flash.h"

#include <platform.h>
#include <spi.h>
#include <stdint.h>
#include <string.h>
#include <sysctl.h>

#define SPI3_CS_MASK        0x01u
#define SPI3_READ_CMD       0x03u
#define SPI3_RX_DUMMY       0x00u
#define SPI3_READ_CHUNK     256u

#define SPI3_MOD_OFF        8u
#define SPI3_DFS_OFF        0u
#define SPI3_TMOD_OFF       10u
#define SPI3_FRF_OFF        22u

#define SPI_CTRL_TMOD_FULL  (0u << SPI3_TMOD_OFF)
#define SPI_CTRL_FRAME_STD  (0u << SPI3_FRF_OFF)
#define SPI_CTRL_MODE0      (0u << SPI3_MOD_OFF)
#define SPI_CTRL_DFS8       (7u << SPI3_DFS_OFF)

static volatile spi_t *const SPI3 = (volatile spi_t *)SPI3_BASE_ADDR;

static void boot_flash_spi3_init(void)
{
    sysctl_clock_enable(SYSCTL_CLOCK_SPI3);
    SPI3->ssienr = 0;
    SPI3->ser = 0;
    SPI3->baudr = 8;
    SPI3->imr = 0;
    SPI3->dmacr = 0;
    SPI3->dmatdlr = 0x10;
    SPI3->dmardlr = 0;
    SPI3->rx_sample_delay = 0;
    SPI3->spi_ctrlr0 = 0;
    SPI3->ctrlr0 = SPI_CTRL_MODE0 | SPI_CTRL_FRAME_STD | SPI_CTRL_TMOD_FULL | SPI_CTRL_DFS8;
    (void)SPI3->icr;
}

static uint8_t spi3_xfer8(uint8_t tx)
{
    while ((SPI3->sr & 0x02u) == 0)
        ;
    SPI3->dr[0] = tx;
    while ((SPI3->sr & 0x08u) == 0)
        ;
    return (uint8_t)SPI3->dr[0];
}

int boot_flash_read(uint32_t flash_offset, void *dst, uint32_t len)
{
    uint8_t *out = (uint8_t *)dst;
    if (!out && len)
        return -1;

    boot_flash_spi3_init();

    while (len) {
        uint32_t chunk = len > SPI3_READ_CHUNK ? SPI3_READ_CHUNK : len;
        uint32_t addr = flash_offset;

        SPI3->ssienr = 1;
        SPI3->ser = SPI3_CS_MASK;

        (void)spi3_xfer8(SPI3_READ_CMD);
        (void)spi3_xfer8((uint8_t)(addr >> 16));
        (void)spi3_xfer8((uint8_t)(addr >> 8));
        (void)spi3_xfer8((uint8_t)(addr >> 0));

        for (uint32_t i = 0; i < chunk; ++i)
            out[i] = spi3_xfer8(SPI3_RX_DUMMY);

        while ((SPI3->sr & 0x01u) != 0)
            ;
        SPI3->ser = 0;
        SPI3->ssienr = 0;

        out += chunk;
        flash_offset += chunk;
        len -= chunk;
    }

    return 0;
}

int boot_flash_read_app_header(uint32_t slot_offset, boot_app_header_t *out)
{
    if (!out)
        return -1;
    memset(out, 0, sizeof(*out));
    return boot_flash_read(slot_offset, out, sizeof(*out));
}

int boot_flash_load_app_image(const boot_app_header_t *hdr)
{
    if (!hdr)
        return -1;
    return boot_flash_read(APP_SLOT0_FLASH_OFFSET, (void *)hdr->load_addr, hdr->image_size);
}
