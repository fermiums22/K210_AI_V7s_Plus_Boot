#!/usr/bin/env python3
from pathlib import Path


def patch_spi_threshold(root: Path) -> bool:
    path = root / "lib" / "bsp" / "device" / "spi.cpp"
    text = path.read_text(encoding="utf-8")
    old = '#define SPI_TRANSMISSION_THRESHOLD 0x80UL  /* 512B SD block with 32-bit frames => DMA */'
    new = '#define SPI_TRANSMISSION_THRESHOLD 0x10000UL  /* boot: keep SPI polling, no HAL DMA runtime */'
    if new in text:
        return False
    if old not in text:
        raise SystemExit(f"ERROR: SPI threshold pattern not found: {path}")
    path.write_text(text.replace(old, new), encoding="utf-8")
    return True


def patch_filesystem_debug(root: Path) -> bool:
    path = root / "lib" / "freertos" / "kernel" / "storage" / "filesystem.cpp"
    text = path.read_text(encoding="utf-8")
    changed = False
    if "#include <uarths.h>" not in text:
        text = text.replace("#include <ff.h>\n", "#include <ff.h>\n#include <uarths.h>\n")
        changed = True
    if "#include <cstdio>" not in text:
        text = text.replace("#include <cstring>\n", "#include <cstring>\n#include <cstdio>\n")
        changed = True
    old_err = """    if (result != FR_OK)
        throw std::runtime_error(err_str[result]);"""
    new_err = """    if (result != FR_OK) {
        char line[32];
        snprintf(line, sizeof(line), "KBOOT:FATFS_ERR %d\\n", (int)result);
        uarths_puts(line);
        throw std::runtime_error(err_str[result]);
    }"""
    if new_err not in text:
        if old_err not in text:
            raise SystemExit(f"ERROR: fatfs error pattern not found: {path}")
        text = text.replace(old_err, new_err)
        changed = True
    noisy = """        auto &st = fs->get_storage();

        uarths_puts("KBOOT:DISK_READ_BEGIN\\n");
        st.read_blocks(sector, count, { buff, ptrdiff_t(st.get_rw_block_size() * count) });
        uarths_puts("KBOOT:DISK_READ_OK\\n");
        return RES_OK;"""
    quiet = """        auto &st = fs->get_storage();

        st.read_blocks(sector, count, { buff, ptrdiff_t(st.get_rw_block_size() * count) });
        return RES_OK;"""
    if noisy in text:
        text = text.replace(noisy, quiet)
        changed = True
    path.write_text(text, encoding="utf-8")
    return changed


def patch_sdcard_no_dma(root: Path) -> bool:
    path = root / "lib" / "drivers" / "src" / "storage" / "sdcard.cpp"
    text = path.read_text(encoding="utf-8")
    changed = False
    replacements = {
        "        sd_read_sector_dma(buffer.data(), start_block, blocks_count);":
            "        sd_read_sector(buffer.data(), start_block, blocks_count);",
        "        sd_write_sector_dma(buffer.data(), start_block, blocks_count);":
            "        sd_write_sector(const_cast<uint8_t *>(buffer.data()), start_block, blocks_count);",
    }
    for old, new in replacements.items():
        if new in text:
            continue
        if old not in text:
            raise SystemExit(f"ERROR: sdcard pattern not found: {path}")
        text = text.replace(old, new)
        changed = True
    path.write_text(text, encoding="utf-8")
    return changed


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    changed = patch_spi_threshold(root)
    changed = patch_filesystem_debug(root) or changed
    changed = patch_sdcard_no_dma(root) or changed
    print("BOOT_SDK_PATCH " + ("updated" if changed else "already-applied"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
