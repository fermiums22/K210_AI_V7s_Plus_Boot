#include "boot_flash.h"
#include "log.h"

#include <platform.h>
#include <spi.h>
#include <stdint.h>
#include <string.h>
#include <sysctl.h>

#define SPI3_CS_MASK             0x01u
#define SPI3_READ_CMD            0x0bu
#define SPI3_QUAD_READ_CMD       0x6bu
#define SPI3_JEDEC_ID_CMD        0x9fu
#define SPI3_READ_SR1_CMD        0x05u
#define SPI3_READ_SR2_CMD        0x35u
#define SPI3_WRITE_ENABLE_CMD    0x06u
#define SPI3_WRITE_SR1_SR2_CMD   0x01u
#define SPI3_WRITE_SR2_CMD       0x31u
#define SPI3_READ_CHUNK          (32u * 1024u)
#define SPI3_LOAD_STEP           (256u * 1024u)
#define SPI3_LOAD_LOG_STEP       (1024u * 1024u)
#define SPI3_TIMEOUT             50000u
#define SPI3_FLASH_READY_TIMEOUT 1000000u
#define SPI3_FLUSH_LIMIT         128u
#define BOOT_CYCLE_HZ            390000000ull

/* SPI3 flash clocking:
 *   sysctl SPI3 clock = source / ((threshold + 1) * 2)
 *   DW SPI SCK        = SPI3 clock / baudr
 * Old code selected IN0=26 MHz, threshold=1, baudr=2 => ~3.25 MHz SCK.
 * Use PLL0 with a conservative divider.  On the usual 780 MHz PLL0 this gives
 * SPI3 clock ~78 MHz and SCK ~39 MHz, still below common flash limits but much
 * faster than the ROM-safe IN0 setup. */
#define SPI3_CLK_SELECT_PLL0 1u
#define SPI3_CLK_THRESHOLD   4u
#define SPI3_BAUDR           2u

#define SPI3_SR_BUSY        0x01u
#define SPI3_SR_TFNF        0x02u
#define SPI3_SR_TFE         0x04u
#define SPI3_SR_RFNE        0x08u

#define SPI3_MOD_OFF        8u
#define SPI3_DFS_OFF        0u
#define SPI3_TMOD_OFF       10u
#define SPI3_FRF_OFF        22u

#define SPI_TMOD_FULL_DUPLEX 0u
#define SPI_TMOD_TX_ONLY     1u
#define SPI_TMOD_RX_ONLY     2u
#define SPI_TMOD_EEPROM_READ 3u

#define SPI_CTRL_FRAME_STD   (0u << SPI3_FRF_OFF)
#define SPI_CTRL_FRAME_QUAD  (2u << SPI3_FRF_OFF)
#define SPI_CTRL_MODE0       (0u << SPI3_MOD_OFF)
#define SPI_CTRL_DFS8        (7u << SPI3_DFS_OFF)

#define SPI3_TRANS_TYPE_OFF      0u
#define SPI3_ADDR_L_OFF          2u
#define SPI3_INST_L_OFF          8u
#define SPI3_WAIT_CYCLES_OFF     11u
#define SPI3_TRANS_TYPE_1C1A     0u
#define SPI3_ADDR_L_24BIT        6u
#define SPI3_INST_L_8BIT         2u
#define SPI3_QUAD_READ_DUMMY     8u

#define FLASH_SR1_BUSY_MASK      0x01u
#define FLASH_SR2_QE_MASK        0x02u

static volatile spi_t *const SPI3 = (volatile spi_t *)SPI3_BASE_ADDR;
static uint8_t spi3_clock_log_done;
static uint8_t spi3_quad_checked;
static int spi3_quad_ready_rc;

static uint64_t boot_cycle_read(void)
{
    uint64_t v;
    __asm__ volatile("rdcycle %0" : "=r"(v));
    return v;
}

static void spi3_flush_rx_bounded(void)
{
    for (uint32_t n = 0; n < SPI3_FLUSH_LIMIT; ++n) {
        if ((SPI3->sr & SPI3_SR_RFNE) == 0)
            break;
        (void)SPI3->dr[0];
    }
}

static int spi3_wait_mask(uint32_t mask, uint32_t value)
{
    for (uint32_t n = 0; n < SPI3_TIMEOUT; ++n) {
        if ((SPI3->sr & mask) == value)
            return 0;
    }
    return -1;
}

static void boot_flash_spi3_init(void)
{
    /* Do not hard-reset SPI3 here.  The ROM has just used the same controller
     * to fetch this boot image from flash; resetting it during early boot has
     * proven to cause reset/hang loops on some boards.  Reprogram only the
     * transaction registers we own. */
    sysctl_clock_set_clock_select(SYSCTL_CLOCK_SELECT_SPI3, SPI3_CLK_SELECT_PLL0);
    sysctl_clock_set_threshold(SYSCTL_THRESHOLD_SPI3, SPI3_CLK_THRESHOLD);
    sysctl_clock_enable(SYSCTL_CLOCK_SPI3);

    SPI3->ssienr = 0;
    SPI3->ser = 0;
    SPI3->baudr = SPI3_BAUDR;
    SPI3->imr = 0;
    SPI3->dmacr = 0;
    SPI3->dmatdlr = 0x10;
    SPI3->dmardlr = 0;
    SPI3->rx_sample_delay = 0;
    SPI3->spi_ctrlr0 = 0;
    SPI3->ctrlr0 = SPI_CTRL_MODE0 | SPI_CTRL_FRAME_STD | SPI_CTRL_DFS8;
    (void)SPI3->icr;
    spi3_flush_rx_bounded();

    if (!spi3_clock_log_done) {
        uint32_t spi3_clk = sysctl_clock_get_freq(SYSCTL_CLOCK_SPI3);
        uint32_t sck = spi3_clk / SPI3_BAUDR;
        LOGF("BOOT_SPI3_CLK sel=%u th=%u baudr=%u spi3=%lu sck=%lu",
             (unsigned)SPI3_CLK_SELECT_PLL0,
             (unsigned)SPI3_CLK_THRESHOLD,
             (unsigned)SPI3_BAUDR,
             (unsigned long)spi3_clk,
             (unsigned long)sck);
        spi3_clock_log_done = 1;
    }
}

static int spi3_set_tmod(uint32_t tmod)
{
    (void)spi3_wait_mask(SPI3_SR_BUSY, 0);
    SPI3->ssienr = 0;
    SPI3->spi_ctrlr0 = 0;
    SPI3->ctrlr0 = SPI_CTRL_MODE0 | SPI_CTRL_FRAME_STD | SPI_CTRL_DFS8 | (tmod << SPI3_TMOD_OFF);
    return 0;
}

static void spi3_deassert(void)
{
    SPI3->ser = 0;
    (void)spi3_wait_mask(SPI3_SR_BUSY, 0);
    SPI3->ssienr = 0;
    spi3_flush_rx_bounded();
}

static int spi3_eeprom_read(const uint8_t *cmd, uint32_t cmd_len, uint8_t *rx, uint32_t rx_len)
{
    uint32_t got = 0;
    if (!cmd || !cmd_len || (!rx && rx_len))
        return -1;
    if (rx_len == 0)
        return 0;

    if (spi3_set_tmod(SPI_TMOD_EEPROM_READ) != 0)
        return -2;

    SPI3->ctrlr1 = rx_len - 1u;
    SPI3->ssienr = 1;

    for (uint32_t i = 0; i < cmd_len; ++i) {
        if (spi3_wait_mask(SPI3_SR_TFNF, SPI3_SR_TFNF) != 0) {
            spi3_deassert();
            return -3;
        }
        SPI3->dr[0] = cmd[i];
    }

    SPI3->ser = SPI3_CS_MASK;

    while (got < rx_len) {
        uint32_t progressed = 0;
        for (uint32_t n = 0; n < SPI3_TIMEOUT; ++n) {
            while ((SPI3->sr & SPI3_SR_RFNE) && got < rx_len) {
                rx[got++] = (uint8_t)SPI3->dr[0];
                progressed = 1;
            }
            if (progressed)
                break;
        }
        if (!progressed) {
            spi3_deassert();
            return -4;
        }
    }

    spi3_deassert();
    return 0;
}

static int spi3_send_cmd(const uint8_t *cmd, uint32_t cmd_len)
{
    if (!cmd || !cmd_len)
        return -1;

    if (spi3_set_tmod(SPI_TMOD_TX_ONLY) != 0)
        return -2;

    SPI3->ssienr = 1;

    for (uint32_t i = 0; i < cmd_len; ++i) {
        if (spi3_wait_mask(SPI3_SR_TFNF, SPI3_SR_TFNF) != 0) {
            spi3_deassert();
            return -3;
        }
        SPI3->dr[0] = cmd[i];
    }

    SPI3->ser = SPI3_CS_MASK;

    if (spi3_wait_mask(SPI3_SR_TFE, SPI3_SR_TFE) != 0) {
        spi3_deassert();
        return -4;
    }
    if (spi3_wait_mask(SPI3_SR_BUSY, 0) != 0) {
        spi3_deassert();
        return -5;
    }

    spi3_deassert();
    return 0;
}

static int spi3_read_reg(uint8_t cmd, uint8_t *value)
{
    if (!value)
        return -1;
    *value = 0xffu;
    return spi3_eeprom_read(&cmd, 1, value, 1);
}

static int spi3_read_jedec_id_raw(uint8_t id[3])
{
    uint8_t cmd = SPI3_JEDEC_ID_CMD;
    if (!id)
        return -1;
    id[0] = 0xffu;
    id[1] = 0xffu;
    id[2] = 0xffu;
    return spi3_eeprom_read(&cmd, 1, id, 3);
}

static int spi3_write_enable(void)
{
    const uint8_t cmd = SPI3_WRITE_ENABLE_CMD;
    return spi3_send_cmd(&cmd, 1);
}

static int spi3_wait_flash_ready(void)
{
    for (uint32_t n = 0; n < SPI3_FLASH_READY_TIMEOUT; ++n) {
        uint8_t sr1 = 0xffu;
        int rc = spi3_read_reg(SPI3_READ_SR1_CMD, &sr1);
        if (rc != 0)
            return rc;
        if ((sr1 & FLASH_SR1_BUSY_MASK) == 0)
            return 0;
    }
    return -1;
}

static int spi3_write_status2_31(uint8_t sr2)
{
    uint8_t cmd[2] = { SPI3_WRITE_SR2_CMD, sr2 };
    int rc = spi3_write_enable();
    if (rc != 0)
        return rc;
    rc = spi3_send_cmd(cmd, sizeof(cmd));
    if (rc != 0)
        return rc;
    return spi3_wait_flash_ready();
}

static int spi3_write_status1_status2_01(uint8_t sr1, uint8_t sr2)
{
    uint8_t cmd[3] = { SPI3_WRITE_SR1_SR2_CMD, sr1, sr2 };
    int rc = spi3_write_enable();
    if (rc != 0)
        return rc;
    rc = spi3_send_cmd(cmd, sizeof(cmd));
    if (rc != 0)
        return rc;
    return spi3_wait_flash_ready();
}

static int spi3_jedec_is_w25q128_like(uint32_t jedec)
{
    uint8_t manufacturer = (uint8_t)(jedec >> 16);
    uint8_t capacity = (uint8_t)jedec;

    /* Be conservative: only auto-write QE for Winbond W25Q128-like NOR.
     * Other chips may already have QE set; if not, fail loud instead of
     * writing status registers blindly. */
    return (manufacturer == 0xefu && capacity == 0x18u);
}

static int spi3_try_enable_qe(uint32_t jedec, uint8_t sr1, uint8_t sr2)
{
    uint8_t new_sr2 = sr2 | FLASH_SR2_QE_MASK;
    uint8_t check_sr2 = 0xffu;
    int rc;

    if (!spi3_jedec_is_w25q128_like(jedec)) {
        LOGF("BOOT_SPI_QE_UNSAFE jedec=0x%06lx", (unsigned long)jedec);
        return -1;
    }

    LOGF("BOOT_SPI_QE_SET cmd=0x%02x sr2=0x%02x",
         (unsigned)SPI3_WRITE_SR2_CMD,
         (unsigned)new_sr2);
    rc = spi3_write_status2_31(new_sr2);
    if (rc == 0 && spi3_read_reg(SPI3_READ_SR2_CMD, &check_sr2) == 0 &&
        (check_sr2 & FLASH_SR2_QE_MASK))
        return 0;

    LOGF("BOOT_SPI_QE_SET cmd=0x%02x sr1=0x%02x sr2=0x%02x",
         (unsigned)SPI3_WRITE_SR1_SR2_CMD,
         (unsigned)sr1,
         (unsigned)new_sr2);
    rc = spi3_write_status1_status2_01(sr1, new_sr2);
    if (rc != 0)
        return rc;

    if (spi3_read_reg(SPI3_READ_SR2_CMD, &check_sr2) != 0)
        return -2;
    return (check_sr2 & FLASH_SR2_QE_MASK) ? 0 : -3;
}

static int spi3_quad_prepare(void)
{
    uint8_t id[3];
    uint8_t sr1 = 0xffu;
    uint8_t sr2 = 0xffu;
    uint32_t jedec;
    int rc;

    boot_flash_spi3_init();

    if (spi3_quad_checked)
        return spi3_quad_ready_rc;

    spi3_quad_checked = 1;

    rc = spi3_read_jedec_id_raw(id);
    if (rc != 0) {
        LOGF("BOOT_SPI_JEDEC rc=%d", rc);
        spi3_quad_ready_rc = rc;
        return rc;
    }

    jedec = ((uint32_t)id[0] << 16) | ((uint32_t)id[1] << 8) | id[2];
    LOGF("BOOT_SPI_JEDEC 0x%06lx", (unsigned long)jedec);

    rc = spi3_read_reg(SPI3_READ_SR1_CMD, &sr1);
    if (rc != 0) {
        spi3_quad_ready_rc = rc;
        return rc;
    }
    rc = spi3_read_reg(SPI3_READ_SR2_CMD, &sr2);
    if (rc != 0) {
        spi3_quad_ready_rc = rc;
        return rc;
    }

    LOGF("BOOT_SPI_STATUS sr1=0x%02x sr2=0x%02x qe=%u",
         (unsigned)sr1,
         (unsigned)sr2,
         (unsigned)((sr2 & FLASH_SR2_QE_MASK) != 0));

    if ((sr2 & FLASH_SR2_QE_MASK) == 0) {
        rc = spi3_try_enable_qe(jedec, sr1, sr2);
        if (rc != 0) {
            spi3_quad_ready_rc = rc;
            return rc;
        }

        rc = spi3_read_reg(SPI3_READ_SR1_CMD, &sr1);
        if (rc != 0) {
            spi3_quad_ready_rc = rc;
            return rc;
        }
        rc = spi3_read_reg(SPI3_READ_SR2_CMD, &sr2);
        if (rc != 0) {
            spi3_quad_ready_rc = rc;
            return rc;
        }

        LOGF("BOOT_SPI_STATUS sr1=0x%02x sr2=0x%02x qe=%u",
             (unsigned)sr1,
             (unsigned)sr2,
             (unsigned)((sr2 & FLASH_SR2_QE_MASK) != 0));
    }

    if ((sr2 & FLASH_SR2_QE_MASK) == 0) {
        spi3_quad_ready_rc = -10;
        return spi3_quad_ready_rc;
    }

    LOGF("BOOT_QUAD_DIRECT cmd=0x%02x addr_bits=24 dummy=%u",
         (unsigned)SPI3_QUAD_READ_CMD,
         (unsigned)SPI3_QUAD_READ_DUMMY);

    spi3_quad_ready_rc = 0;
    return 0;
}

static int spi3_quad_read_6b(uint32_t addr, uint8_t *rx, uint32_t rx_len)
{
    uint32_t got = 0;
    uint32_t cmd[2] = { SPI3_QUAD_READ_CMD, addr };

    if (!rx && rx_len)
        return -1;
    if (rx_len == 0)
        return 0;

    if (spi3_wait_mask(SPI3_SR_BUSY, 0) != 0)
        return -2;

    SPI3->ssienr = 0;
    SPI3->ser = 0;
    SPI3->spi_ctrlr0 =
        (SPI3_TRANS_TYPE_1C1A << SPI3_TRANS_TYPE_OFF) |
        (SPI3_ADDR_L_24BIT << SPI3_ADDR_L_OFF) |
        (SPI3_INST_L_8BIT << SPI3_INST_L_OFF) |
        (SPI3_QUAD_READ_DUMMY << SPI3_WAIT_CYCLES_OFF);
    SPI3->ctrlr0 = SPI_CTRL_MODE0 | SPI_CTRL_FRAME_QUAD | SPI_CTRL_DFS8 |
                   (SPI_TMOD_EEPROM_READ << SPI3_TMOD_OFF);
    SPI3->ctrlr1 = rx_len - 1u;
    SPI3->ssienr = 1;

    for (uint32_t i = 0; i < 2; ++i) {
        if (spi3_wait_mask(SPI3_SR_TFNF, SPI3_SR_TFNF) != 0) {
            spi3_deassert();
            return -3;
        }
        SPI3->dr[0] = cmd[i];
    }

    SPI3->ser = SPI3_CS_MASK;

    while (got < rx_len) {
        uint32_t progressed = 0;
        for (uint32_t n = 0; n < SPI3_TIMEOUT; ++n) {
            while ((SPI3->sr & SPI3_SR_RFNE) && got < rx_len) {
                rx[got++] = (uint8_t)SPI3->dr[0];
                progressed = 1;
            }
            if (progressed)
                break;
        }
        if (!progressed) {
            spi3_deassert();
            return -4;
        }
    }

    spi3_deassert();
    return 0;
}

uint32_t boot_flash_read_jedec_id(void)
{
    uint8_t id[3] = {0xff, 0xff, 0xff};

    boot_flash_spi3_init();
    if (spi3_read_jedec_id_raw(id) != 0)
        return 0xffffffffu;

    return ((uint32_t)id[0] << 16) | ((uint32_t)id[1] << 8) | id[2];
}

int boot_flash_read(uint32_t flash_offset, void *dst, uint32_t len)
{
    uint8_t *out = (uint8_t *)dst;
    if (!out && len)
        return -1;

    int prep_rc = spi3_quad_prepare();
    if (prep_rc != 0)
        return prep_rc;

    while (len) {
        uint32_t chunk = len > SPI3_READ_CHUNK ? SPI3_READ_CHUNK : len;
        int rc = spi3_quad_read_6b(flash_offset, out, chunk);
        if (rc != 0)
            return rc;

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
    uint32_t done = 0;
    uint32_t next_log = SPI3_LOAD_LOG_STEP;
    uint64_t t0;
    uint64_t t1;
    uint64_t dt_cycles;
    uint64_t ms;
    uint64_t kib_s;

    if (!hdr)
        return -1;

    LOGF("BOOT_LOAD_BEGIN flash=0x%08lx ram=0x%08lx size=%lu",
         (unsigned long)APP_SLOT0_FLASH_OFFSET,
         (unsigned long)hdr->load_addr,
         (unsigned long)hdr->image_size);

    t0 = boot_cycle_read();

    while (done < hdr->image_size) {
        uint32_t left = hdr->image_size - done;
        uint32_t step = left > SPI3_LOAD_STEP ? SPI3_LOAD_STEP : left;
        int rc = boot_flash_read(APP_SLOT0_FLASH_OFFSET + done,
                                 (void *)(uintptr_t)(hdr->load_addr + done),
                                 step);
        if (rc != 0) {
            LOGF("BOOT_LOAD_READ_FAIL offset=0x%08lx done=%lu rc=%d",
                 (unsigned long)(APP_SLOT0_FLASH_OFFSET + done),
                 (unsigned long)done,
                 rc);
            return rc;
        }

        done += step;
        if (done >= next_log || done >= hdr->image_size) {
            LOGF("BOOT_LOAD_PROGRESS %lu/%lu",
                 (unsigned long)done,
                 (unsigned long)hdr->image_size);
            next_log += SPI3_LOAD_LOG_STEP;
        }
    }

    t1 = boot_cycle_read();
    dt_cycles = t1 - t0;
    ms = (dt_cycles * 1000ull) / BOOT_CYCLE_HZ;
    if (ms == 0)
        ms = 1;
    kib_s = (((uint64_t)hdr->image_size / 1024ull) * 1000ull) / ms;

    LOGF("BOOT_LOAD_DONE mode=quad bytes=%lu ms=%lu KiB/s=%lu",
         (unsigned long)hdr->image_size,
         (unsigned long)ms,
         (unsigned long)kib_s);
    return 0;
}
