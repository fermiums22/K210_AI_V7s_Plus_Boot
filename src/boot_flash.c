#include "boot_flash.h"
#include "log.h"

#include <dmac.h>
#include <platform.h>
#include <spi.h>
#include <stdint.h>
#include <string.h>
#include <sysctl.h>

#ifndef DMAC_BASE_ADDR
#define DMAC_BASE_ADDR       0x50000000UL
#endif

#define SPI3_CS_MASK          0x01u
#define SPI3_QUAD_READ_CMD    0x6bu
#define SPI3_JEDEC_ID_CMD     0x9fu
#define SPI3_READ_SR1_CMD     0x05u
#define SPI3_READ_SR2_CMD     0x35u
#define SPI3_QUAD_READ_CHUNK  (32u * 1024u)
#define SPI3_LOAD_STEP        (256u * 1024u)
#define SPI3_LOAD_LOG_STEP    (1024u * 1024u)
#define SPI3_TIMEOUT          50000u
#define SPI3_DMA_TIMEOUT      5000000u
#define SPI3_FLUSH_LIMIT      128u
#define BOOT_CYCLE_HZ         390000000ull
#define FLASH_SR2_QE_MASK     0x02u

/* SPI3 flash clocking:
 *   sysctl SPI3 clock = source / ((threshold + 1) * 2)
 *   DW SPI SCK        = SPI3 clock / baudr
 * PLL0 threshold=2 gives 780 MHz / ((2 + 1) * 2) / 2 = ~65 MHz SCK. */
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

#define SPI3_TRANS_TYPE_OFF  0u
#define SPI3_ADDR_L_OFF      2u
#define SPI3_INST_L_OFF      8u
#define SPI3_WAIT_CYCLES_OFF 11u

#define SPI_TMOD_EEPROM_READ 3u

#define SPI_CTRL_FRAME_STD   (0u << SPI3_FRF_OFF)
#define SPI_CTRL_FRAME_QUAD  (2u << SPI3_FRF_OFF)
#define SPI_CTRL_MODE0       (0u << SPI3_MOD_OFF)
#define SPI_CTRL_DFS8        (7u << SPI3_DFS_OFF)

#define SPI3_TRANS_TYPE_1C1A    0u
#define SPI3_ADDR_L_24BIT       6u
#define SPI3_INST_L_8BIT        2u
#define SPI3_QUAD_DUMMY_CYCLES  8u

/* K210 DW_ahb_dmac layout.  Unlike the generic DW-DMAC map, channel 0 starts
 * at DMAC+0x100 and block_ts is a separate register at channel+0x10. */
#define BOOT_DMAC_CH               0u
#define BOOT_DMAC_CH_MASK          (1ull << BOOT_DMAC_CH)
#define BOOT_DMAC_CH_WE            (1ull << (BOOT_DMAC_CH + 8u))

#define BOOT_DMAC_WIDTH_8          0u
#define BOOT_DMAC_INC              0u
#define BOOT_DMAC_NOCHANGE         1u
#define BOOT_DMAC_MSIZE_16         3u
#define BOOT_DMAC_TT_FC_P2M        2u
#define BOOT_DMAC_PRIOR_HI         7u

#define BOOT_DMAC_CTL_SMS(v)       ((uint64_t)(v) << 0)
#define BOOT_DMAC_CTL_DMS(v)       ((uint64_t)(v) << 2)
#define BOOT_DMAC_CTL_SINC(v)      ((uint64_t)(v) << 4)
#define BOOT_DMAC_CTL_DINC(v)      ((uint64_t)(v) << 6)
#define BOOT_DMAC_CTL_SRC_WIDTH(v) ((uint64_t)(v) << 8)
#define BOOT_DMAC_CTL_DST_WIDTH(v) ((uint64_t)(v) << 11)
#define BOOT_DMAC_CTL_SRC_MSIZE(v) ((uint64_t)(v) << 14)
#define BOOT_DMAC_CTL_DST_MSIZE(v) ((uint64_t)(v) << 18)
#define BOOT_DMAC_CTL_IOC_BLK      (1ull << 58)

#define BOOT_DMAC_CFG_TT_FC(v)     ((uint64_t)(v) << 32)
#define BOOT_DMAC_CFG_HS_SRC_HW    (0ull << 35)
#define BOOT_DMAC_CFG_HS_DST_SW    (1ull << 36)
#define BOOT_DMAC_CFG_SRC_PER(v)   ((uint64_t)(v) << 39)
#define BOOT_DMAC_CFG_DST_PER(v)   ((uint64_t)(v) << 44)
#define BOOT_DMAC_CFG_CH_PRI(v)    ((uint64_t)(v) << 49)

#define BOOT_DMAC_INT_BLOCK_DONE   0x01ull
#define BOOT_DMAC_INT_DMA_DONE     0x02ull
#define BOOT_DMAC_INT_DONE_MASK    (BOOT_DMAC_INT_BLOCK_DONE | BOOT_DMAC_INT_DMA_DONE)
#define BOOT_DMAC_INT_ERR_MASK     0x1fe0ull

#define SPI3_DMA_SELECT_RX         SYSCTL_DMA_SELECT_SSI3_RX_REQ

static volatile spi_t *const SPI3 = (volatile spi_t *)SPI3_BASE_ADDR;
static volatile dmac_t *const BOOT_DMAC = (volatile dmac_t *)DMAC_BASE_ADDR;
static uint8_t spi3_clock_log_done;
static uint8_t spi3_flash_id_log_done;
static uint8_t spi3_quad_log_done;
static uint8_t spi3_dma_init_done;

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

static void spi3_dma_clear_ints(void)
{
    BOOT_DMAC->channel[BOOT_DMAC_CH].intclear = 0xffffffffull;
}

static void spi3_dma_disable_channel(void)
{
    BOOT_DMAC->chen = BOOT_DMAC_CH_WE;
}

static void spi3_dma_enable_channel(void)
{
    BOOT_DMAC->chen = BOOT_DMAC_CH_WE | BOOT_DMAC_CH_MASK;
}

static void spi3_dma_init_once(void)
{
    if (spi3_dma_init_done)
        return;
    sysctl_clock_enable(SYSCTL_CLOCK_DMA);
    BOOT_DMAC->cfg = 0x3ull; /* DMAC enable + interrupt/status generation enable. */
    spi3_dma_disable_channel();
    spi3_dma_clear_ints();
    BOOT_DMAC->channel[BOOT_DMAC_CH].intstatus_en = BOOT_DMAC_INT_DONE_MASK | BOOT_DMAC_INT_ERR_MASK;
    BOOT_DMAC->channel[BOOT_DMAC_CH].intsignal_en = 0;
    sysctl_dma_select((sysctl_dma_channel_t)BOOT_DMAC_CH, SPI3_DMA_SELECT_RX);
    spi3_dma_init_done = 1;
}

static void spi3_dma_start_rx(uint8_t *dst, uint32_t len)
{
    spi3_dma_disable_channel();
    spi3_dma_clear_ints();

    BOOT_DMAC->channel[BOOT_DMAC_CH].cfg =
        BOOT_DMAC_CFG_TT_FC(BOOT_DMAC_TT_FC_P2M) |
        BOOT_DMAC_CFG_HS_SRC_HW |
        BOOT_DMAC_CFG_HS_DST_SW |
        BOOT_DMAC_CFG_SRC_PER(BOOT_DMAC_CH) |
        BOOT_DMAC_CFG_DST_PER(BOOT_DMAC_CH) |
        BOOT_DMAC_CFG_CH_PRI(BOOT_DMAC_PRIOR_HI);

    BOOT_DMAC->channel[BOOT_DMAC_CH].sar = (uint64_t)(uintptr_t)&SPI3->dr[0];
    BOOT_DMAC->channel[BOOT_DMAC_CH].dar = (uint64_t)(uintptr_t)dst;
    BOOT_DMAC->channel[BOOT_DMAC_CH].llp = 0;
    BOOT_DMAC->channel[BOOT_DMAC_CH].block_ts = (uint64_t)len - 1ull;
    BOOT_DMAC->channel[BOOT_DMAC_CH].ctl =
        BOOT_DMAC_CTL_SMS(0) |
        BOOT_DMAC_CTL_DMS(1) |
        BOOT_DMAC_CTL_SINC(BOOT_DMAC_NOCHANGE) |
        BOOT_DMAC_CTL_DINC(BOOT_DMAC_INC) |
        BOOT_DMAC_CTL_SRC_WIDTH(BOOT_DMAC_WIDTH_8) |
        BOOT_DMAC_CTL_DST_WIDTH(BOOT_DMAC_WIDTH_8) |
        BOOT_DMAC_CTL_SRC_MSIZE(BOOT_DMAC_MSIZE_16) |
        BOOT_DMAC_CTL_DST_MSIZE(BOOT_DMAC_MSIZE_16) |
        BOOT_DMAC_CTL_IOC_BLK;

    (void)BOOT_DMAC->channel[BOOT_DMAC_CH].swhssrc;
    spi3_dma_enable_channel();
}

static int spi3_dma_wait_done(void)
{
    for (uint32_t n = 0; n < SPI3_DMA_TIMEOUT; ++n) {
        uint64_t intstatus = BOOT_DMAC->channel[BOOT_DMAC_CH].intstatus;
        if (intstatus & BOOT_DMAC_INT_ERR_MASK) {
            spi3_dma_clear_ints();
            spi3_dma_disable_channel();
            return -2;
        }
        if (intstatus & BOOT_DMAC_INT_DONE_MASK) {
            spi3_dma_clear_ints();
            spi3_dma_disable_channel();
            return 0;
        }
    }
    spi3_dma_disable_channel();
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
    spi3_dma_init_once();

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
    SPI3->dmacr = 0;
    SPI3->ctrlr0 = SPI_CTRL_MODE0 | SPI_CTRL_FRAME_STD | SPI_CTRL_DFS8 | (tmod << SPI3_TMOD_OFF);
    return 0;
}

static void spi3_deassert(void)
{
    SPI3->ser = 0;
    (void)spi3_wait_mask(SPI3_SR_BUSY, 0);
    SPI3->dmacr = 0;
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

static int spi3_quad_read_dma_6b(uint32_t addr, uint8_t *rx, uint32_t rx_len)
{
    int dma_rc;

    if (!rx && rx_len)
        return -1;
    if (rx_len == 0)
        return 0;

    (void)spi3_wait_mask(SPI3_SR_BUSY, 0);
    SPI3->ssienr = 0;
    SPI3->ser = 0;
    SPI3->dmacr = 0;
    SPI3->spi_ctrlr0 =
        (SPI3_TRANS_TYPE_1C1A << SPI3_TRANS_TYPE_OFF) |
        (SPI3_ADDR_L_24BIT << SPI3_ADDR_L_OFF) |
        (SPI3_INST_L_8BIT << SPI3_INST_L_OFF) |
        (SPI3_QUAD_DUMMY_CYCLES << SPI3_WAIT_CYCLES_OFF);
    SPI3->ctrlr0 = SPI_CTRL_MODE0 | SPI_CTRL_FRAME_QUAD | SPI_CTRL_DFS8 |
                   (SPI_TMOD_EEPROM_READ << SPI3_TMOD_OFF);
    SPI3->ctrlr1 = rx_len - 1u;
    SPI3->dmardlr = 0;
    SPI3->dmacr = 0x1; /* RX DMA enable. */
    SPI3->ssienr = 1;

    if (spi3_wait_mask(SPI3_SR_TFNF, SPI3_SR_TFNF) != 0) {
        spi3_deassert();
        return -3;
    }
    SPI3->dr[0] = SPI3_QUAD_READ_CMD;

    if (spi3_wait_mask(SPI3_SR_TFNF, SPI3_SR_TFNF) != 0) {
        spi3_deassert();
        return -3;
    }
    SPI3->dr[0] = addr & 0x00ffffffu;

    spi3_dma_start_rx(rx, rx_len);
    SPI3->ser = SPI3_CS_MASK;

    dma_rc = spi3_dma_wait_done();
    if (dma_rc == 0) {
        spi3_deassert();
        return 0;
    }

    {
        uint32_t sr = SPI3->sr;
        uint32_t rxflr = SPI3->rxflr;
        uint64_t ch_intstatus = BOOT_DMAC->channel[BOOT_DMAC_CH].intstatus;
        uint64_t ch_status = BOOT_DMAC->channel[BOOT_DMAC_CH].status;
        uint64_t ch_block = BOOT_DMAC->channel[BOOT_DMAC_CH].block_ts;
        uint64_t ch_ctl = BOOT_DMAC->channel[BOOT_DMAC_CH].ctl;
        uint64_t ch_cfg = BOOT_DMAC->channel[BOOT_DMAC_CH].cfg;
        uint64_t chen = BOOT_DMAC->chen;
        spi3_deassert();
        LOGF("BOOT_QUAD_DMA_FAIL rc=%d addr=0x%08lx len=%lu sr=0x%08lx rxflr=%lu chint=0x%lx chstat=0x%lx block=0x%lx ctl=0x%lx cfg=0x%lx chen=0x%lx",
             dma_rc,
             (unsigned long)addr,
             (unsigned long)rx_len,
             (unsigned long)sr,
             (unsigned long)rxflr,
             (unsigned long)ch_intstatus,
             (unsigned long)ch_status,
             (unsigned long)ch_block,
             (unsigned long)ch_ctl,
             (unsigned long)ch_cfg,
             (unsigned long)chen);
    }
    return -4;
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

static void spi3_log_flash_id_once(void)
{
    uint8_t id[3];
    uint8_t sr1 = 0xffu;
    uint8_t sr2 = 0xffu;
    uint32_t jedec;
    int id_rc;
    int sr1_rc;
    int sr2_rc;

    if (spi3_flash_id_log_done)
        return;
    spi3_flash_id_log_done = 1;

    id_rc = spi3_read_jedec_id_raw(id);
    if (id_rc == 0) {
        jedec = ((uint32_t)id[0] << 16) | ((uint32_t)id[1] << 8) | id[2];
        LOGF("BOOT_SPI_JEDEC 0x%06lx", (unsigned long)jedec);
    } else {
        LOGF("BOOT_SPI_JEDEC rc=%d", id_rc);
    }

    sr1_rc = spi3_read_reg(SPI3_READ_SR1_CMD, &sr1);
    sr2_rc = spi3_read_reg(SPI3_READ_SR2_CMD, &sr2);
    if (sr1_rc == 0 && sr2_rc == 0) {
        LOGF("BOOT_SPI_STATUS sr1=0x%02x sr2=0x%02x qe=%u",
             (unsigned)sr1,
             (unsigned)sr2,
             (unsigned)((sr2 & FLASH_SR2_QE_MASK) != 0));
    } else {
        LOGF("BOOT_SPI_STATUS rc1=%d rc2=%d", sr1_rc, sr2_rc);
    }
}

static void spi3_log_quad_once(void)
{
    if (spi3_quad_log_done)
        return;
    spi3_quad_log_done = 1;
    LOGF("BOOT_QUAD_DIRECT cmd=0x%02x addr_bits=24 dummy=%u chunk=%lu dma=k210-direct msize=16 sck=65MHz",
         (unsigned)SPI3_QUAD_READ_CMD,
         (unsigned)SPI3_QUAD_DUMMY_CYCLES,
         (unsigned long)SPI3_QUAD_READ_CHUNK);
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

    boot_flash_spi3_init();
    spi3_log_flash_id_once();
    spi3_log_quad_once();

    while (len) {
        uint32_t chunk = len > SPI3_QUAD_READ_CHUNK ? SPI3_QUAD_READ_CHUNK : len;
        int rc = spi3_quad_read_dma_6b(flash_offset, out, chunk);
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

    LOGF("BOOT_LOAD_DONE mode=quad-dma bytes=%lu ms=%lu KiB/s=%lu",
         (unsigned long)hdr->image_size,
         (unsigned long)ms,
         (unsigned long)kib_s);
    return 0;
}
