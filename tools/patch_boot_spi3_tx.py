#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATH = ROOT / "src" / "boot_cmd.c"

TX_OLD = """static int spi3_tx(const uint8_t *tx, uint32_t tx_len)
{
    if (!tx || tx_len == 0)
        return -1;

    spi3_std_init();
    (void)spi3_wait_mask(SPI3_SR_BUSY, 0);
    SPI3->ssienr = 0;
    SPI3->spi_ctrlr0 = 0;
    SPI3->ctrlr0 = SPI_CTRL_MODE0 | SPI_CTRL_FRAME_STD | SPI_CTRL_DFS8 |
                   (SPI_TMOD_TX_ONLY << SPI3_TMOD_OFF);
    SPI3->ssienr = 1;

    for (uint32_t i = 0; i < tx_len; i++) {
        if (spi3_wait_mask(SPI3_SR_TFNF, SPI3_SR_TFNF) != 0) {
            spi3_deassert();
            return -2;
        }
        SPI3->dr[0] = tx[i];
    }

    SPI3->ser = SPI3_CS_MASK;
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
"""

TX_NEW = """static int spi3_tx(const uint8_t *tx, uint32_t tx_len)
{
    if (!tx || tx_len == 0)
        return -1;

    spi3_std_init();
    (void)spi3_wait_mask(SPI3_SR_BUSY, 0);
    SPI3->ssienr = 0;
    SPI3->spi_ctrlr0 = 0;
    /* Use plain TX/RX for write-side flash commands and drain exactly one RX
     * byte for every TX byte.  Without this, RX FIFO fills after 8 frames and
     * the page-program stream effectively stops after the command+address+4B.
     */
    SPI3->ctrlr0 = SPI_CTRL_MODE0 | SPI_CTRL_FRAME_STD | SPI_CTRL_DFS8 |
                   (SPI_TMOD_TX_RX << SPI3_TMOD_OFF);
    SPI3->ssienr = 1;
    SPI3->ser = SPI3_CS_MASK;

    for (uint32_t i = 0; i < tx_len; i++) {
        if (spi3_wait_mask(SPI3_SR_TFNF, SPI3_SR_TFNF) != 0) {
            spi3_deassert();
            return -2;
        }
        SPI3->dr[0] = tx[i];
        if (spi3_wait_mask(SPI3_SR_RFNE, SPI3_SR_RFNE) != 0) {
            spi3_deassert();
            return -5;
        }
        (void)SPI3->dr[0];
    }

    if (spi3_wait_mask(SPI3_SR_TFE, SPI3_SR_TFE) != 0) {
        spi3_deassert();
        return -3;
    }
    if (spi3_wait_mask(SPI3_SR_BUSY, 0) != 0) {
        spi3_deassert();
        return -4;
    }
    spi3_flush_rx();
    spi3_deassert();
    return 0;
}
"""

PP_OLD = """    s_buf[0] = SPI3_PAGE_PROGRAM_CMD;
    s_buf[1] = (uint8_t)((offset >> 16) & 0xffu);
    s_buf[2] = (uint8_t)((offset >> 8) & 0xffu);
    s_buf[3] = (uint8_t)(offset & 0xffu);
    memcpy(&s_buf[4], data, len);

    int rc = spi3_write_enable();
    if (rc != 0)
        return -10 + rc;
    rc = spi3_tx(s_buf, len + 4u);
"""

PP_NEW = """    /* Do not use s_buf here: callers use it as the expected payload for
     * readback verification.  Build the program command in s_verify instead.
     */
    s_verify[0] = SPI3_PAGE_PROGRAM_CMD;
    s_verify[1] = (uint8_t)((offset >> 16) & 0xffu);
    s_verify[2] = (uint8_t)((offset >> 8) & 0xffu);
    s_verify[3] = (uint8_t)(offset & 0xffu);
    memcpy(&s_verify[4], data, len);

    int rc = spi3_write_enable();
    if (rc != 0)
        return -10 + rc;
    rc = spi3_tx(s_verify, len + 4u);
"""


def replace_once(text: str, old: str, new: str, label: str) -> tuple[str, int]:
    if new in text:
        return text, 0
    if old not in text:
        raise SystemExit(f"PATCH_FAIL boot_cmd.c {label} pattern not found")
    return text.replace(old, new, 1), 1


def main() -> int:
    if not PATH.exists():
        raise SystemExit(f"PATCH_FAIL missing {PATH}")
    text = PATH.read_text(encoding="utf-8")
    text, tx_changed = replace_once(text, TX_OLD, TX_NEW, "spi3_tx")
    text, pp_changed = replace_once(text, PP_OLD, PP_NEW, "page_program_buffer")
    if tx_changed or pp_changed:
        PATH.write_text(text, encoding="utf-8")
    print(f"BOOT_SPI3_TX_PATCH_OK tx={tx_changed} pagebuf={pp_changed}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
