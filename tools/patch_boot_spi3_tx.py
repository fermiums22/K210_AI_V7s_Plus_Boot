#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATH = ROOT / "src" / "boot_cmd.c"

OLD = """    SPI3->ssienr = 1;

    for (uint32_t i = 0; i < tx_len; i++) {
        if (spi3_wait_mask(SPI3_SR_TFNF, SPI3_SR_TFNF) != 0) {
            spi3_deassert();
            return -2;
        }
        SPI3->dr[0] = tx[i];
    }

    SPI3->ser = SPI3_CS_MASK;
"""

NEW = """    SPI3->ssienr = 1;
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


def main() -> int:
    if not PATH.exists():
        raise SystemExit(f"PATCH_FAIL missing {PATH}")
    text = PATH.read_text(encoding="utf-8")
    if NEW in text:
        print("BOOT_SPI3_TX_PATCH_OK already-applied")
        return 0
    if OLD not in text:
        raise SystemExit("PATCH_FAIL boot_cmd.c spi3_tx pattern not found")
    PATH.write_text(text.replace(OLD, NEW, 1), encoding="utf-8")
    print("BOOT_SPI3_TX_PATCH_OK applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
