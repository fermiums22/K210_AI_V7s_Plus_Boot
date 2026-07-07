#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATH = ROOT / "src" / "boot_cmd.c"

TX_OLD = """    SPI3->ssienr = 1;

    for (uint32_t i = 0; i < tx_len; i++) {
        if (spi3_wait_mask(SPI3_SR_TFNF, SPI3_SR_TFNF) != 0) {
            spi3_deassert();
            return -2;
        }
        SPI3->dr[0] = tx[i];
    }

    SPI3->ser = SPI3_CS_MASK;
"""

TX_NEW = """    SPI3->ssienr = 1;
    /* SER must be asserted before filling long TX-only transfers.
     * Otherwise page-program payloads larger than the SPI FIFO never start
     * shifting and spi3_tx() times out waiting for TFNF.
     */
    SPI3->ser = SPI3_CS_MASK;

    for (uint32_t i = 0; i < tx_len; i++) {
        if (spi3_wait_mask(SPI3_SR_TFNF, SPI3_SR_TFNF) != 0) {
            spi3_deassert();
            return -2;
        }
        SPI3->dr[0] = tx[i];
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
