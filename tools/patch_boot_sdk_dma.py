#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def write_if_changed(path: Path, old_text: str, new_text: str) -> bool:
    if old_text != new_text:
        path.write_text(new_text, encoding="utf-8")
        return True
    return False


def cleanup_duplicates(path: Path) -> int:
    text = path.read_text(encoding="utf-8")
    before = text
    duplicate_pairs = [
        (
            "        spi32_dev_ = make_accessor(spi->get_device(SPI_MODE_0, SPI_FF_STANDARD, 1, 32));\n"
            "        spi32_dev_ = make_accessor(spi->get_device(SPI_MODE_0, SPI_FF_STANDARD, 1, 32));",
            "        spi32_dev_ = make_accessor(spi->get_device(SPI_MODE_0, SPI_FF_STANDARD, 1, 32));",
        ),
        (
            "        spi32_dev_.reset();\n"
            "        spi32_dev_.reset();",
            "        spi32_dev_.reset();",
        ),
        (
            "    object_accessor<spi_device_driver> spi32_dev_;\n"
            "    object_accessor<spi_device_driver> spi32_dev_;",
            "    object_accessor<spi_device_driver> spi32_dev_;",
        ),
    ]
    changed = 0
    for old, new in duplicate_pairs:
        while old in text:
            text = text.replace(old, new)
            changed += 1
    if write_if_changed(path, before, text):
        return changed
    return 0


def replace_one(path: Path, old: str, new: str) -> bool:
    text = path.read_text(encoding="utf-8")
    if new in text:
        return False
    if old not in text:
        raise SystemExit(f"PATCH_FAIL {path}: pattern not found")
    return write_if_changed(path, text, text.replace(old, new, 1))


def replace_all(path: Path, old: str, new: str) -> int:
    text = path.read_text(encoding="utf-8")
    if new in text and old not in text:
        return 0
    n = text.count(old)
    if not n:
        raise SystemExit(f"PATCH_FAIL {path}: pattern not found")
    write_if_changed(path, text, text.replace(old, new))
    return n


def patch_spi_cpp() -> int:
    path = ROOT / "lib" / "bsp" / "device" / "spi.cpp"
    if not path.exists():
        raise SystemExit(f"PATCH_FAIL missing {path}")
    changed = cleanup_duplicates(path)
    changed += int(replace_one(
        path,
        "#define SPI_TRANSMISSION_THRESHOLD 0x800UL",
        "#define SPI_TRANSMISSION_THRESHOLD 0x80UL  /* 512B SD block with 32-bit frames => DMA */",
    ))
    changed += replace_all(
        path,
        "dma_transmit_async(dma_read, &spi_.dr[0], buffer_read, 0, 1, device.buffer_width_, rx_frames, 1, event_read);",
        "dma_transmit_async(dma_read, &spi_.dr[0], buffer_read, 0, 1, device.buffer_width_, rx_frames, 16, event_read);",
    )
    changed += replace_all(
        path,
        "dma_transmit_async(dma_write, buffer_write, &spi_.dr[0], 1, 0, device.buffer_width_, tx_frames, 4, event_write);",
        "dma_transmit_async(dma_write, buffer_write, &spi_.dr[0], 1, 0, device.buffer_width_, tx_frames, 16, event_write);",
    )
    return changed


def patch_sdcard_cpp() -> int:
    path = ROOT / "lib" / "drivers" / "src" / "storage" / "sdcard.cpp"
    if not path.exists():
        raise SystemExit(f"PATCH_FAIL missing {path}")
    changed = cleanup_duplicates(path)
    changed += int(replace_one(
        path,
        "        spi8_dev_ = make_accessor(spi->get_device(SPI_MODE_0, SPI_FF_STANDARD, 1, 8));\n\n        cs_gpio_ = make_accessor(cs_gpio_driver_);",
        "        spi8_dev_ = make_accessor(spi->get_device(SPI_MODE_0, SPI_FF_STANDARD, 1, 8));\n        spi32_dev_ = make_accessor(spi->get_device(SPI_MODE_0, SPI_FF_STANDARD, 1, 32));\n\n        cs_gpio_ = make_accessor(cs_gpio_driver_);",
    ))
    changed += int(replace_one(
        path,
        "        spi8_dev_.reset();\n        cs_gpio_.reset();",
        "        spi32_dev_.reset();\n        spi8_dev_.reset();\n        cs_gpio_.reset();",
    ))
    changed += int(replace_one(
        path,
        "    bool init_ok_ = false;\n\n    void set_tf_cs_low()",
        "    bool init_ok_ = false;\n    uint32_t dma_block_[128] __attribute__((aligned(64)));\n\n    static uint32_t bswap32(uint32_t v)\n    {\n        return ((v & 0x000000ffu) << 24) |\n               ((v & 0x0000ff00u) << 8) |\n               ((v & 0x00ff0000u) >> 8) |\n               ((v & 0xff000000u) >> 24);\n    }\n\n    void set_tf_cs_low()",
    ))
    changed += int(replace_one(
        path,
        "    void sd_write_data_dma(const uint8_t *data_buff)\n    {\n        spi8_dev_->write({ data_buff, 512L });\n    }\n\n    void sd_read_data_dma(uint8_t *data_buff)\n    {\n        spi8_dev_->read({ data_buff, 512L });\n    }",
        "    void sd_write_data_dma(const uint8_t *data_buff)\n    {\n        memcpy(dma_block_, data_buff, 512);\n        for (size_t i = 0; i < 128; i++)\n            dma_block_[i] = bswap32(dma_block_[i]);\n        spi32_dev_->write({ reinterpret_cast<const uint8_t *>(dma_block_), 512L });\n    }\n\n    void sd_read_data_dma(uint8_t *data_buff)\n    {\n        spi32_dev_->read({ reinterpret_cast<uint8_t *>(dma_block_), 512L });\n        for (size_t i = 0; i < 128; i++)\n            dma_block_[i] = bswap32(dma_block_[i]);\n        memcpy(data_buff, dma_block_, 512);\n    }",
    ))
    changed += int(replace_one(
        path,
        "    object_accessor<gpio_driver> cs_gpio_;\n    object_accessor<spi_device_driver> spi8_dev_;",
        "    object_accessor<gpio_driver> cs_gpio_;\n    object_accessor<spi_device_driver> spi8_dev_;\n    object_accessor<spi_device_driver> spi32_dev_;",
    ))
    return changed


def patch_boot_cmd_c() -> int:
    path = ROOT / "src" / "boot_cmd.c"
    if not path.exists():
        raise SystemExit(f"PATCH_FAIL missing {path}")
    changed = 0
    changed += int(replace_one(
        path,
        "static uint8_t s_verify[BOOT_CMD_BUF] __attribute__((aligned(64)));\n",
        "static uint8_t s_verify[BOOT_CMD_BUF] __attribute__((aligned(64)));\n\nextern void boot_sdk_install_drivers_once(void);\n",
    ))
    changed += int(replace_one(
        path,
        "static bool sd_rw_probe(void)\n{\n    if (!sd_mount())",
        "static bool sd_rw_probe(void)\n{\n    host_puts(\"KBOOT:SD_SDK_INIT\\n\");\n    boot_sdk_install_drivers_once();\n    host_puts(\"KBOOT:SD_SDK_INIT_OK\\n\");\n\n    if (!sd_mount())",
    ))
    return changed


def main() -> int:
    changed_spi = patch_spi_cpp()
    changed_sd = patch_sdcard_cpp()
    changed_cmd = patch_boot_cmd_c()
    print(f"BOOT_SDK_DMA_PATCH_OK spi_changes={changed_spi} sd_changes={changed_sd} cmd_changes={changed_cmd}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
