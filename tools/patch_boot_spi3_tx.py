#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BOOT_CMD = ROOT / "src" / "boot_cmd.c"
BOOT_FLASH_C = ROOT / "src" / "boot_flash.c"
BOOT_FLASH_H = ROOT / "src" / "boot_flash.h"


def replace_once(text: str, old: str, new: str, label: str) -> tuple[str, int]:
    if new in text:
        return text, 0
    if old not in text:
        raise SystemExit(f"PATCH_FAIL {label} pattern not found")
    return text.replace(old, new, 1), 1


def patch_header() -> int:
    text = BOOT_FLASH_H.read_text(encoding="utf-8")
    old = """int boot_flash_read(uint32_t flash_offset, void *dst, uint32_t len);
int boot_flash_read_app_header(uint32_t slot_offset, boot_app_header_t *out);
int boot_flash_load_app_image(const boot_app_header_t *hdr);
uint32_t boot_flash_read_jedec_id(void);
"""
    new = """int boot_flash_read(uint32_t flash_offset, void *dst, uint32_t len);
int boot_flash_read_app_header(uint32_t slot_offset, boot_app_header_t *out);
int boot_flash_load_app_image(const boot_app_header_t *hdr);
uint32_t boot_flash_read_jedec_id(void);
int boot_flash_read_status(uint8_t *sr1, uint8_t *sr2);
int boot_flash_erase_4k(uint32_t flash_offset);
int boot_flash_program(uint32_t flash_offset, const void *src, uint32_t len);
"""
    text, changed = replace_once(text, old, new, "boot_flash.h api")
    if changed:
        BOOT_FLASH_H.write_text(text, encoding="utf-8")
    return changed


def patch_flash_c() -> int:
    text = BOOT_FLASH_C.read_text(encoding="utf-8")

    define_old = """#define SPI3_READ_SR1_CMD     0x05u
#define SPI3_READ_SR2_CMD     0x35u
#define SPI3_QUAD_READ_CHUNK  (32u * 1024u)
"""
    define_new = """#define SPI3_READ_SR1_CMD     0x05u
#define SPI3_READ_SR2_CMD     0x35u
#define SPI3_WRITE_ENABLE_CMD 0x06u
#define SPI3_PAGE_PROGRAM_CMD 0x02u
#define SPI3_SECTOR_ERASE_CMD 0x20u
#define SPI3_QUAD_READ_CHUNK  (32u * 1024u)
"""
    text, d1 = replace_once(text, define_old, define_new, "boot_flash.c cmd defines")

    define2_old = """#define SPI3_DMA_TIMEOUT      5000000u
#define SPI3_FLUSH_LIMIT      128u
#define BOOT_CYCLE_HZ         390000000ull
#define FLASH_SR2_QE_MASK     0x02u
"""
    define2_new = """#define SPI3_DMA_TIMEOUT      5000000u
#define SPI3_WIP_TIMEOUT      8000000u
#define SPI3_FLUSH_LIMIT      128u
#define BOOT_CYCLE_HZ         390000000ull
#define FLASH_SR1_WIP_MASK    0x01u
#define FLASH_SR1_WEL_MASK    0x02u
#define FLASH_SR2_QE_MASK     0x02u
"""
    text, d2 = replace_once(text, define2_old, define2_new, "boot_flash.c status defines")

    define3_old = """#define SPI_TMOD_EEPROM_READ 3u

#define SPI_CTRL_FRAME_STD   (0u << SPI3_FRF_OFF)
"""
    define3_new = """#define SPI_TMOD_TX_RX       0u
#define SPI_TMOD_TX_ONLY     1u
#define SPI_TMOD_EEPROM_READ 3u

#define SPI_CTRL_FRAME_STD   (0u << SPI3_FRF_OFF)
"""
    text, d3 = replace_once(text, define3_old, define3_new, "boot_flash.c tmod defines")

    define4_old = """#define SPI3_BAUDR           2u

#define SPI3_SR_BUSY        0x01u
"""
    define4_new = """#define SPI3_BAUDR           2u
#define SPI3_BAUDR_WRITE     8u

#define SPI3_SR_BUSY        0x01u
"""
    text, d4 = replace_once(text, define4_old, define4_new, "boot_flash.c write baud")

    helper_marker = """static int spi3_read_jedec_id_raw(uint8_t id[3])
"""
    helper_code = r'''static void spi3_prepare_std(uint32_t tmod, uint32_t baudr)
{
    boot_flash_spi3_init();

    /* One owner for SPI3 state: every STD transaction explicitly tears down
     * DMA/quad-read leftovers before switching to command/status/program mode.
     */
    spi3_dma_disable_channel();
    spi3_dma_clear_ints();

    (void)spi3_wait_mask(SPI3_SR_BUSY, 0);
    SPI3->ser = 0;
    SPI3->ssienr = 0;
    SPI3->dmacr = 0;
    SPI3->spi_ctrlr0 = 0;
    SPI3->ctrlr1 = 0;
    SPI3->baudr = baudr;
    SPI3->endian = SPI3_ENDIAN_NORMAL;
    SPI3->ctrlr0 = SPI_CTRL_MODE0 | SPI_CTRL_FRAME_STD | SPI_CTRL_DFS8 |
                   (tmod << SPI3_TMOD_OFF);
    (void)SPI3->icr;
    spi3_flush_rx_bounded();
}

static int spi3_std_tx_only(const uint8_t *tx, uint32_t tx_len)
{
    if (!tx || tx_len == 0)
        return -1;

    spi3_prepare_std(SPI_TMOD_TX_ONLY, SPI3_BAUDR_WRITE);
    SPI3->ssienr = 1;
    SPI3->ser = SPI3_CS_MASK;

    for (uint32_t i = 0; i < tx_len; ++i) {
        if (spi3_wait_mask(SPI3_SR_TFNF, SPI3_SR_TFNF) != 0) {
            spi3_deassert();
            return -2;
        }
        SPI3->dr[0] = tx[i];
    }

    if (spi3_wait_mask(SPI3_SR_TFE, SPI3_SR_TFE) != 0) {
        spi3_deassert();
        return -3;
    }
    if (spi3_wait_mask(SPI3_SR_BUSY, 0) != 0) {
        spi3_deassert();
        return -4;
    }

    spi3_deassert();
    return 0;
}

static int spi3_flash_read_sr1(uint8_t *sr1)
{
    return spi3_read_reg(SPI3_READ_SR1_CMD, sr1);
}

static int spi3_flash_write_enable(void)
{
    uint8_t cmd = SPI3_WRITE_ENABLE_CMD;
    uint8_t sr1 = 0xffu;
    int rc = spi3_std_tx_only(&cmd, 1);
    if (rc != 0)
        return -10 + rc;

    rc = spi3_flash_read_sr1(&sr1);
    LOGF("BOOT_SPI3_WREN rc=%d sr1=0x%02x", rc, (unsigned)sr1);
    if (rc != 0)
        return -20 + rc;
    if ((sr1 & FLASH_SR1_WEL_MASK) == 0)
        return -30;
    return 0;
}

static int spi3_flash_wait_wip_clear(const char *tag)
{
    uint8_t last_sr1 = 0xffu;
    for (uint32_t n = 0; n < SPI3_WIP_TIMEOUT; ++n) {
        uint8_t sr1 = 0xffu;
        int rc = spi3_flash_read_sr1(&sr1);
        last_sr1 = sr1;
        if (rc != 0) {
            LOGF("BOOT_SPI3_%s_SR_FAIL rc=%d sr1=0x%02x", tag, rc, (unsigned)sr1);
            return -1;
        }
        if (sr1 == 0xffu) {
            LOGF("BOOT_SPI3_%s_BUS_FF sr1=0x%02x", tag, (unsigned)sr1);
            return -6;
        }
        if ((sr1 & FLASH_SR1_WIP_MASK) == 0) {
            LOGF("BOOT_SPI3_%s_DONE sr1=0x%02x", tag, (unsigned)sr1);
            return 0;
        }
    }
    LOGF("BOOT_SPI3_%s_TIMEOUT sr1=0x%02x", tag, (unsigned)last_sr1);
    return -2;
}

'''
    if helper_code not in text:
        if helper_marker not in text:
            raise SystemExit("PATCH_FAIL boot_flash.c helper marker not found")
        text = text.replace(helper_marker, helper_code + helper_marker, 1)
        helpers = 1
    else:
        helpers = 0

    api_marker = """uint32_t boot_flash_read_jedec_id(void)
"""
    api_code = r'''int boot_flash_read_status(uint8_t *sr1, uint8_t *sr2)
{
    int rc1 = 0;
    int rc2 = 0;
    if (sr1) {
        *sr1 = 0xffu;
        rc1 = spi3_read_reg(SPI3_READ_SR1_CMD, sr1);
    }
    if (sr2) {
        *sr2 = 0xffu;
        rc2 = spi3_read_reg(SPI3_READ_SR2_CMD, sr2);
    }
    if (rc1 != 0)
        return -10 + rc1;
    if (rc2 != 0)
        return -20 + rc2;
    return 0;
}

int boot_flash_erase_4k(uint32_t flash_offset)
{
    uint8_t cmd[4] = {
        SPI3_SECTOR_ERASE_CMD,
        (uint8_t)((flash_offset >> 16) & 0xffu),
        (uint8_t)((flash_offset >> 8) & 0xffu),
        (uint8_t)(flash_offset & 0xffu)
    };
    int rc;

    if ((flash_offset & 0xfffu) != 0)
        return -1;

    rc = spi3_flash_write_enable();
    if (rc != 0)
        return -10 + rc;

    rc = spi3_std_tx_only(cmd, sizeof(cmd));
    LOGF("BOOT_SPI3_ERASE_TX off=0x%08lx rc=%d", (unsigned long)flash_offset, rc);
    if (rc != 0)
        return -20 + rc;

    rc = spi3_flash_wait_wip_clear("ERASE");
    if (rc != 0)
        return -30 + rc;
    return 0;
}

int boot_flash_program(uint32_t flash_offset, const void *src, uint32_t len)
{
    const uint8_t *in = (const uint8_t *)src;
    uint8_t cmd[4 + 256];
    int rc;

    if (!in || len == 0 || len > 256u)
        return -1;
    if (((flash_offset & 0xffu) + len) > 256u)
        return -2;

    cmd[0] = SPI3_PAGE_PROGRAM_CMD;
    cmd[1] = (uint8_t)((flash_offset >> 16) & 0xffu);
    cmd[2] = (uint8_t)((flash_offset >> 8) & 0xffu);
    cmd[3] = (uint8_t)(flash_offset & 0xffu);
    memcpy(&cmd[4], in, len);

    rc = spi3_flash_write_enable();
    if (rc != 0)
        return -10 + rc;

    rc = spi3_std_tx_only(cmd, len + 4u);
    LOGF("BOOT_SPI3_PROG_TX off=0x%08lx len=%lu rc=%d",
         (unsigned long)flash_offset, (unsigned long)len, rc);
    if (rc != 0)
        return -20 + rc;

    rc = spi3_flash_wait_wip_clear("PROG");
    if (rc != 0)
        return -30 + rc;
    return 0;
}

'''
    if api_code not in text:
        if api_marker not in text:
            raise SystemExit("PATCH_FAIL boot_flash.c api marker not found")
        text = text.replace(api_marker, api_code + api_marker, 1)
        api = 1
    else:
        api = 0

    changed = d1 + d2 + d3 + d4 + helpers + api
    if changed:
        BOOT_FLASH_C.write_text(text, encoding="utf-8")
    return changed


def patch_boot_cmd() -> int:
    text = BOOT_CMD.read_text(encoding="utf-8")
    old = """    int rc = spi3_sector_erase_4k(offset);
    if (rc != 0) {
        snprintf(detail, detail_size, "erase-rc=%d", rc);
        return false;
    }

    rc = spi3_page_program(offset, s_buf, BOOT_SPI3_SCRATCH_SIZE);
    if (rc != 0) {
        snprintf(detail, detail_size, "prog-rc=%d", rc);
        return false;
    }

    rc = boot_flash_read(offset, s_verify, BOOT_SPI3_SCRATCH_SIZE);
"""
    new = """    int rc = boot_flash_erase_4k(offset);
    if (rc != 0) {
        snprintf(detail, detail_size, "erase-rc=%d", rc);
        return false;
    }

    rc = boot_flash_program(offset, s_buf, BOOT_SPI3_SCRATCH_SIZE);
    if (rc != 0) {
        snprintf(detail, detail_size, "prog-rc=%d", rc);
        return false;
    }

    rc = boot_flash_read(offset, s_verify, BOOT_SPI3_SCRATCH_SIZE);
"""
    text, changed = replace_once(text, old, new, "boot_cmd.c spi3 rw api calls")
    if changed:
        BOOT_CMD.write_text(text, encoding="utf-8")
    return changed


def main() -> int:
    for p in (BOOT_CMD, BOOT_FLASH_C, BOOT_FLASH_H):
        if not p.exists():
            raise SystemExit(f"PATCH_FAIL missing {p}")

    h = patch_header()
    c = patch_flash_c()
    cmd = patch_boot_cmd()
    print(f"BOOT_SPI3_API_PATCH_OK header={h} flash={c} cmd={cmd}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
