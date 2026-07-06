#include "boot_flash.h"

#include <platform.h>
#include <spi.h>
#include <stdint.h>
#include <string.h>
#include <sysctl.h>

#define SPI3_CS_MASK        0x01u
#define SPI3_READ_CMD       0x03u
#define SPI3_JEDEC_ID_CMD   0x9fu
#define SPI3_RX_DUMMY       0x00u
#define SPI3_READ_CHUNK     256u
#define SPI3_TIMEOUT        1000000u

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
    /*
     * The boot decision runs before the full SDK runtime. Do not rely on ROM or
     * kflash ISP side effects for SPI3 clocking; explicitly put SPI3 on the
     * safe 26 MHz IN0 clock and divide it down before talking to the flash.
     */
    sysctl_clock_set_clock_select(SYSCTL_CLOCK_SELECT_SPI3, 0); /* IN0 */
    sysctl_clock_set_threshold(SYSCTL_THRESHOLD_SPI3, 1);       /* 26M / 4 */
    sysctl_clock_enable(SYSCTL_CLOCK_SPI3);
    sysctl_reset(SYSCTL_RESET_SPI3);

    SPI3->ssienr = 0;
    SPI3->ser = 0;
    SPI3->baudr = 4;
    SPI3->imr = 0;
    SPI3->dmacr = 0;
    SPI3->dmatdlr = 0x10;
    SPI3->dmardlr = 0;
    SPI3->rx_sample_delay = 0;
    SPI3->spi_ctrlr0 = 0;
    SPI3->ctrlr0 = SPI_CTRL_MODE0 | SPI_CTRL_FRAME_STD | SPI_CTRL_TMOD_FULL | SPI_CTRL_DFS8;
    (void)SPI3->icr;

    while (SPI3->sr & 0x08u)
        (void)SPI3->dr[0];
}

static int spi3_wait_mask(uint32_t mask, uint32_t value)
{
    for (uint32_t n = 0; n < SPI3_TIMEOUT; ++n) {
        if ((SPI3->sr & mask) == value)
            return 0;
    }
    return -1;
}

static int spi3_xfer8(uint8_t tx, uint8_t *rx)
{
    if (spi3_wait_mask(0x02u, 0x02u) != 0)
        return -1;
    SPI3->dr[0] = tx;
    if (spi3_wait_mask(0x08u, 0x08u) != 0)
        return -2;
    if (rx)
        *rx = (uint8_t)SPI3->dr[0];
    else
        (void)SPI3->dr[0];
    return 0;
}

static void spi3_select(void)
{
    SPI3->ssienr = 1;
    SPI3->ser = SPI3_CS_MASK;
}

static void spi3_deassert(void)
{
    SPI3->ser = 0;
    SPI3->ssienr = 0;
}

uint32_t boot_flash_read_jedec_id(void)
{
    uint8_t b0 = 0xff;
    uint8_t b1 = 0xff;
    uint8_t b2 = 0xff;

    boot_flash_spi3_init();
    spi3_select();
    if (spi3_xfer8(SPI3_JEDEC_ID_CMD, 0) != 0 ||
        spi3_xfer8(SPI3_RX_DUMMY, &b0) != 0 ||
        spi3_xfer8(SPI3_RX_DUMMY, &b1) != 0 ||
        spi3_xfer8(SPI3_RX_DUMMY, &b2) != 0) {
        spi3_deassert();
        return 0xffffffffu;
    }
    spi3_deassert();

    return ((uint32_t)b0 << 16) | ((uint32_t)b1 << 8) | b2;
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

        spi3_select();

        if (spi3_xfer8(SPI3_READ_CMD, 0) != 0 ||
            spi3_xfer8((uint8_t)(addr >> 16), 0) != 0 ||
            spi3_xfer8((uint8_t)(addr >> 8), 0) != 0 ||
            spi3_xfer8((uint8_t)(addr >> 0), 0) != 0) {
            spi3_deassert();
            return -2;
        }

        for (uint32_t i = 0; i < chunk; ++i) {
            if (spi3_xfer8(SPI3_RX_DUMMY, &out[i]) != 0) {
                spi3_deassert();
                return -3;
            }
        }

        if (spi3_wait_mask(0x01u, 0x00u) != 0) {
            spi3_deassert();
            return -4;
        }
        spi3_deassert();

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
    return boot_flash_read(APP_SLOT0_FLASH_OFFSET, (void *)(uintptr_t)hdr->load_addr, hdr->image_size);
}
