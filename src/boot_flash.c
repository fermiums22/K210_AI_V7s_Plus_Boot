#include "boot_flash.h"
#include "log.h"

#include <platform.h>
#include <spi.h>
#include <stdint.h>
#include <string.h>
#include <sysctl.h>

#define SPI3_CS_MASK        0x01u
#define SPI3_READ_CMD       0x0bu
#define SPI3_QUAD_READ_CMD  0x6bu
#define SPI3_JEDEC_ID_CMD   0x9fu
#define SPI3_READ_SR1_CMD   0x05u
#define SPI3_READ_SR2_CMD   0x35u
#define SPI3_WRITE_EN_CMD   0x06u
#define SPI3_WRITE_SR2_CMD  0x31u
#define SPI3_SR1_WIP        0x01u
#define SPI3_SR2_QE         0x02u
#define SPI3_READ_CHUNK     (32u * 1024u)
#define SPI3_LOAD_STEP      (256u * 1024u)
#define SPI3_LOAD_LOG_STEP  (1024u * 1024u)
#define SPI3_TIMEOUT        50000u
#define SPI3_WIP_TIMEOUT    2000000u
#define SPI3_FLUSH_LIMIT    128u
#define BOOT_CYCLE_HZ       390000000ull

/* SPI3 flash clocking:
 *   sysctl SPI3 clock = source / ((threshold + 1) * 2)
 *   DW SPI SCK        = SPI3 clock / baudr
 * PLL0=780 MHz, threshold=2, baudr=2 gives SPI3 clock ~130 MHz and SCK ~65 MHz.
 * Single 0x0B is limited to one data bit per SCK.  Quad 0x6B keeps cmd/address
 * on one line and returns data on IO0..IO3. */
#define SPI3_CLK_SELECT_PLL0 1u
#define SPI3_CLK_THRESHOLD   2u
#define SPI3_BAUDR           2u

#define SPI3_SR_BUSY        0x01u
#define SPI3_SR_TFNF        0x02u
#define SPI3_SR_TFE         0x04u
#define SPI3_SR_RFNE        0x08u

#define SPI3_MOD_OFF        8u
#define SPI3_DFS_OFF        0u
#define SPI3_TMOD_OFF       10u
#define SPI3_FRF_OFF        22u

#define SPI3_SPI_TRANS_OFF  0u
#define SPI3_SPI_ADDR_L_OFF 2u
#define SPI3_SPI_INST_L_OFF 8u
#define SPI3_SPI_WAIT_OFF   11u

#define SPI_TMOD_FULL_DUPLEX 0u
#define SPI_TMOD_TX_ONLY     1u
#define SPI_TMOD_RX_ONLY     2u
#define SPI_TMOD_EEPROM_READ 3u

#define SPI_CTRL_FRAME_STD   (0u << SPI3_FRF_OFF)
#define SPI_CTRL_FRAME_DUAL  (1u << SPI3_FRF_OFF)
#define SPI_CTRL_FRAME_QUAD  (2u << SPI3_FRF_OFF)
#define SPI_CTRL_MODE0       (0u << SPI3_MOD_OFF)
#define SPI_CTRL_DFS8        (7u << SPI3_DFS_OFF)
#define SPI_CTRL_DFS32       (31u << SPI3_DFS_OFF)

#define SPI_XFER_1C1A        0u
#define SPI_XFER_1C2A        1u
#define SPI_XFER_2C2A        2u
#define SPI_INST_8B          2u
#define SPI_ADDR_24B         6u
#define SPI_DUMMY_8CYC       8u

static volatile spi_t *const SPI3 = (volatile spi_t *)SPI3_BASE_ADDR;
static uint8_t spi3_clock_log_done;
static int8_t spi3_quad_checked;
static int8_t spi3_quad_ok;

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

static int spi3_set_tmod_frame(uint32_t tmod, uint32_t frame)
{
    (void)spi3_wait_mask(SPI3_SR_BUSY, 0);
    SPI3->ssienr = 0;
    SPI3->spi_ctrlr0 = 0;
    SPI3->ctrlr0 = SPI_CTRL_MODE0 | frame | (tmod << SPI3_TMOD_OFF);
    return 0;
}

static int spi3_set_tmod(uint32_t tmod)
{
    return spi3_set_tmod_frame(tmod, SPI_CTRL_FRAME_STD | SPI_CTRL_DFS8);
}

static void spi3_deassert(void)
{
    SPI3->ser = 0;
    (void)spi3_wait_mask(SPI3_SR_BUSY, 0);
    SPI3->ssienr = 0;
    SPI3->spi_ctrlr0 = 0;
    spi3_flush_rx_bounded();
}

static int spi3_tx_only(const uint8_t *tx, uint32_t tx_len)
{
    if (!tx || !tx_len)
        return -1;

    if (spi3_set_tmod(SPI_TMOD_TX_ONLY) != 0)
        return -2;

    SPI3->ssienr = 1;
    for (uint32_t i = 0; i < tx_len; ++i) {
        if (spi3_wait_mask(SPI3_SR_TFNF, SPI3_SR_TFNF) != 0) {
            spi3_deassert();
            return -3;
        }
        SPI3->dr[0] = tx[i];
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

static int spi3_read_reg8(uint8_t cmd, uint8_t *value)
{
    return spi3_eeprom_read(&cmd, 1, value, 1);
}

static int spi3_write_enable(void)
{
    uint8_t cmd = SPI3_WRITE_EN_CMD;
    return spi3_tx_only(&cmd, 1);
}

static int spi3_wait_write_done(void)
{
    uint8_t sr1 = 0xffu;
    for (uint32_t n = 0; n < SPI3_WIP_TIMEOUT; ++n) {
        if (spi3_read_reg8(SPI3_READ_SR1_CMD, &sr1) != 0)
            return -1;
        if ((sr1 & SPI3_SR1_WIP) == 0)
            return 0;
    }
    return -2;
}

static int spi3_enable_quad_if_needed(void)
{
    uint8_t sr1 = 0xffu;
    uint8_t sr2 = 0xffu;
    int rc;

    rc = spi3_read_reg8(SPI3_READ_SR1_CMD, &sr1);
    if (rc != 0)
        return rc;
    rc = spi3_read_reg8(SPI3_READ_SR2_CMD, &sr2);
    if (rc != 0)
        return rc;

    LOGF("BOOT_SPI_STATUS sr1=0x%02x sr2=0x%02x qe=%u",
         (unsigned)sr1,
         (unsigned)sr2,
         (unsigned)((sr2 & SPI3_SR2_QE) ? 1 : 0));

    if (sr2 & SPI3_SR2_QE)
        return 0;

    rc = spi3_write_enable();
    if (rc != 0)
        return rc;

    uint8_t wr[2] = { SPI3_WRITE_SR2_CMD, (uint8_t)(sr2 | SPI3_SR2_QE) };
    rc = spi3_tx_only(wr, sizeof(wr));
    if (rc != 0)
        return rc;

    rc = spi3_wait_write_done();
    if (rc != 0)
        return rc;

    rc = spi3_read_reg8(SPI3_READ_SR2_CMD, &sr2);
    if (rc != 0)
        return rc;

    LOGF("BOOT_SPI_QE_SET sr2=0x%02x qe=%u",
         (unsigned)sr2,
         (unsigned)((sr2 & SPI3_SR2_QE) ? 1 : 0));

    return (sr2 & SPI3_SR2_QE) ? 0 : -10;
}

static int spi3_quad_output_read(uint32_t flash_offset, void *dst, uint32_t len)
{
    uint8_t *out = (uint8_t *)dst;
    if (!out && len)
        return -1;
    if (len == 0)
        return 0;

    while (len) {
        uint32_t chunk = len > SPI3_READ_CHUNK ? SPI3_READ_CHUNK : len;
        uint32_t frames = (chunk + 3u) / 4u;
        uint32_t got = 0;
        uint32_t addr = flash_offset & 0x00ffffffu;

        if (spi3_set_tmod_frame(SPI_TMOD_EEPROM_READ, SPI_CTRL_FRAME_QUAD | SPI_CTRL_DFS32) != 0)
            return -2;

        SPI3->spi_ctrlr0 =
            (SPI_XFER_1C1A << SPI3_SPI_TRANS_OFF) |
            (SPI_ADDR_24B << SPI3_SPI_ADDR_L_OFF) |
            (SPI_INST_8B << SPI3_SPI_INST_L_OFF) |
            (SPI_DUMMY_8CYC << SPI3_SPI_WAIT_OFF);
        SPI3->ctrlr1 = frames - 1u;
        SPI3->ssienr = 1;

        if (spi3_wait_mask(SPI3_SR_TFNF, SPI3_SR_TFNF) != 0) {
            spi3_deassert();
            return -3;
        }
        SPI3->dr[0] = SPI3_QUAD_READ_CMD;
        if (spi3_wait_mask(SPI3_SR_TFNF, SPI3_SR_TFNF) != 0) {
            spi3_deassert();
            return -4;
        }
        SPI3->dr[0] = addr;

        SPI3->ser = SPI3_CS_MASK;

        while (got < frames) {
            uint32_t progressed = 0;
            for (uint32_t n = 0; n < SPI3_TIMEOUT; ++n) {
                while ((SPI3->sr & SPI3_SR_RFNE) && got < frames) {
                    uint32_t word = SPI3->dr[0];
                    uint32_t remain = chunk - (got * 4u);
                    uint32_t copy = remain >= 4u ? 4u : remain;
                    for (uint32_t i = 0; i < copy; ++i)
                        out[(got * 4u) + i] = (uint8_t)(word >> (i * 8u));
                    ++got;
                    progressed = 1;
                }
                if (progressed)
                    break;
            }
            if (!progressed) {
                spi3_deassert();
                return -5;
            }
        }

        spi3_deassert();
        out += chunk;
        flash_offset += chunk;
        len -= chunk;
    }

    return 0;
}

uint32_t boot_flash_read_jedec_id(void)
{
    uint8_t cmd = SPI3_JEDEC_ID_CMD;
    uint8_t id[3] = {0xff, 0xff, 0xff};

    boot_flash_spi3_init();
    if (spi3_eeprom_read(&cmd, 1, id, sizeof(id)) != 0)
        return 0xffffffffu;

    return ((uint32_t)id[0] << 16) | ((uint32_t)id[1] << 8) | id[2];
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
        uint8_t cmd[5] = {
            SPI3_READ_CMD,
            (uint8_t)(addr >> 16),
            (uint8_t)(addr >> 8),
            (uint8_t)(addr >> 0),
            0x00u, /* 0x0B fast-read dummy byte */
        };
        int rc = spi3_eeprom_read(cmd, sizeof(cmd), out, chunk);
        if (rc != 0)
            return rc;

        out += chunk;
        flash_offset += chunk;
        len -= chunk;
    }

    return 0;
}

static int boot_flash_quad_probe(const boot_app_header_t *expected)
{
    boot_app_header_t qh;
    uint32_t jedec;
    int rc;

    if (spi3_quad_checked)
        return spi3_quad_ok ? 0 : -1;

    spi3_quad_checked = 1;
    spi3_quad_ok = 0;

    boot_flash_spi3_init();
    jedec = boot_flash_read_jedec_id();
    LOGF("BOOT_SPI_JEDEC 0x%06lx", (unsigned long)(jedec & 0x00ffffffu));

    rc = spi3_enable_quad_if_needed();
    if (rc != 0) {
        LOGF("BOOT_QUAD_DISABLED qe_rc=%d", rc);
        return rc;
    }

    memset(&qh, 0, sizeof(qh));
    rc = spi3_quad_output_read(APP_SLOT0_FLASH_OFFSET, &qh, sizeof(qh));
    if (rc != 0) {
        LOGF("BOOT_QUAD_PROBE_FAIL rc=%d", rc);
        return rc;
    }

    if (qh.magic != expected->magic ||
        qh.magic_inv != expected->magic_inv ||
        qh.load_addr != expected->load_addr ||
        qh.entry_addr != expected->entry_addr ||
        qh.image_size != expected->image_size) {
        LOGF("BOOT_QUAD_PROBE_MISMATCH magic=0x%08lx size=%lu",
             (unsigned long)qh.magic,
             (unsigned long)qh.image_size);
        return -20;
    }

    spi3_quad_ok = 1;
    LOG("BOOT_QUAD_OK cmd=0x6b mode=quad-output dfs=32");
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
    int use_quad = 0;

    if (!hdr)
        return -1;

    LOGF("BOOT_LOAD_BEGIN flash=0x%08lx ram=0x%08lx size=%lu",
         (unsigned long)APP_SLOT0_FLASH_OFFSET,
         (unsigned long)hdr->load_addr,
         (unsigned long)hdr->image_size);

    if (boot_flash_quad_probe(hdr) == 0)
        use_quad = 1;
    else
        LOG("BOOT_LOAD_MODE single-fallback");

    t0 = boot_cycle_read();

    while (done < hdr->image_size) {
        uint32_t left = hdr->image_size - done;
        uint32_t step = left > SPI3_LOAD_STEP ? SPI3_LOAD_STEP : left;
        int rc;
        if (use_quad)
            rc = spi3_quad_output_read(APP_SLOT0_FLASH_OFFSET + done,
                                       (void *)(uintptr_t)(hdr->load_addr + done),
                                       step);
        else
            rc = boot_flash_read(APP_SLOT0_FLASH_OFFSET + done,
                                 (void *)(uintptr_t)(hdr->load_addr + done),
                                 step);
        if (rc != 0) {
            LOGF("BOOT_LOAD_READ_FAIL mode=%s offset=0x%08lx done=%lu rc=%d",
                 use_quad ? "quad" : "single",
                 (unsigned long)(APP_SLOT0_FLASH_OFFSET + done),
                 (unsigned long)done,
                 rc);
            if (use_quad) {
                LOG("BOOT_LOAD_MODE retry-single");
                use_quad = 0;
                done = 0;
                next_log = SPI3_LOAD_LOG_STEP;
                continue;
            }
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

    LOGF("BOOT_LOAD_DONE mode=%s bytes=%lu ms=%lu KiB/s=%lu",
         use_quad ? "quad" : "single",
         (unsigned long)hdr->image_size,
         (unsigned long)ms,
         (unsigned long)kib_s);
    return 0;
}
