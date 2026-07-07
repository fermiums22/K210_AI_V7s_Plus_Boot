#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_once(path: Path, old: str, new: str) -> bool:
    text = path.read_text(encoding="utf-8")
    if new in text:
        return False
    if old not in text:
        raise SystemExit(f"PATCH_FAIL {path}: pattern not found")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")
    return True


def patch_spi_cpp() -> bool:
    path = ROOT / "lib" / "bsp" / "device" / "spi.cpp"
    if not path.exists():
        raise SystemExit(f"PATCH_FAIL missing {path}")
    changed = False
    changed |= replace_once(
        path,
        "#define SPI_TRANSMISSION_THRESHOLD 0x800UL",
        "#define SPI_TRANSMISSION_THRESHOLD 0x80UL  /* 512B SD block with 32-bit frames => DMA */",
    )
    changed |= replace_once(
        path,
        "dma_transmit_async(dma_read, &spi_.dr[0], buffer_read, 0, 1, device.buffer_width_, rx_frames, 1, event_read);",
        "dma_transmit_async(dma_read, &spi_.dr[0], buffer_read, 0, 1, device.buffer_width_, rx_frames, 16, event_read);",
    )
    changed |= replace_once(
        path,
        "dma_transmit_async(dma_read, &spi_.dr[0], buffer_read, 0, 1, device.buffer_width_, rx_frames, 1, event_read);",
        "dma_transmit_async(dma_read, &spi_.dr[0], buffer_read, 0, 1, device.buffer_width_, rx_frames, 16, event_read);",
    )
    changed |= replace_once(
        path,
        "dma_transmit_async(dma_write, buffer_write, &spi_.dr[0], 1, 0, device.buffer_width_, tx_frames, 4, event_write);",
        "dma_transmit_async(dma_write, buffer_write, &spi_.dr[0], 1, 0, device.buffer_width_, tx_frames, 16, event_write);",
    )
    changed |= replace_once(
        path,
        "dma_transmit_async(dma_write, buffer_write, &spi_.dr[0], 1, 0, device.buffer_width_, tx_frames, 4, event_write);",
        "dma_transmit_async(dma_write, buffer_write, &spi_.dr[0], 1, 0, device.buffer_width_, tx_frames, 16, event_write);",
    )
    return changed


def patch_sdcard_cpp() -> bool:
    path = ROOT / "lib" / "drivers" / "src" / "storage" / "sdcard.cpp"
    if not path.exists():
        raise SystemExit(f"PATCH_FAIL missing {path}")
    changed = False
    changed |= replace_once(
        path,
        "        spi8_dev_ = make_accessor(spi->get_device(SPI_MODE_0, SPI_FF_STANDARD, 1, 8));\n\n        cs_gpio_ = make_accessor(cs_gpio_driver_);",
        "        spi8_dev_ = make_accessor(spi->get_device(SPI_MODE_0, SPI_FF_STANDARD, 1, 8));\n        spi32_dev_ = make_accessor(spi->get_device(SPI_MODE_0, SPI_FF_STANDARD, 1, 32));\n\n        cs_gpio_ = make_accessor(cs_gpio_driver_);",
    )
    changed |= replace_once(
        path,
        "        spi8_dev_.reset();\n        cs_gpio_.reset();",
        "        spi32_dev_.reset();\n        spi8_dev_.reset();\n        cs_gpio_.reset();",
    )
    changed |= replace_once(
        path,
        "    bool init_ok_ = false;\n\n    void set_tf_cs_low()",
        "    bool init_ok_ = false;\n    uint32_t dma_block_[128] __attribute__((aligned(64)));\n\n    static uint32_t bswap32(uint32_t v)\n    {\n        return ((v & 0x000000ffu) << 24) |\n               ((v & 0x0000ff00u) << 8) |\n               ((v & 0x00ff0000u) >> 8) |\n               ((v & 0xff000000u) >> 24);\n    }\n\n    void set_tf_cs_low()",
    )
    changed |= replace_once(
        path,
        "    void sd_write_data_dma(const uint8_t *data_buff)\n    {\n        spi8_dev_->write({ data_buff, 512L });\n    }\n\n    void sd_read_data_dma(uint8_t *data_buff)\n    {\n        spi8_dev_->read({ data_buff, 512L });\n    }",
        "    void sd_write_data_dma(const uint8_t *data_buff)\n    {\n        memcpy(dma_block_, data_buff, 512);\n        for (size_t i = 0; i < 128; i++)\n            dma_block_[i] = bswap32(dma_block_[i]);\n        spi32_dev_->write({ reinterpret_cast<const uint8_t *>(dma_block_), 512L });\n    }\n\n    void sd_read_data_dma(uint8_t *data_buff)\n    {\n        spi32_dev_->read({ reinterpret_cast<uint8_t *>(dma_block_), 512L });\n        for (size_t i = 0; i < 128; i++)\n            dma_block_[i] = bswap32(dma_block_[i]);\n        memcpy(data_buff, dma_block_, 512);\n    }",
    )
    changed |= replace_once(
        path,
        "    object_accessor<gpio_driver> cs_gpio_;\n    object_accessor<spi_device_driver> spi8_dev_;",
        "    object_accessor<gpio_driver> cs_gpio_;\n    object_accessor<spi_device_driver> spi8_dev_;\n    object_accessor<spi_device_driver> spi32_dev_;",
    )
    return changed


def main() -> int:
    changed_spi = patch_spi_cpp()
    changed_sd = patch_sdcard_cpp()
    print(f"BOOT_SDK_DMA_PATCH_OK spi_changed={int(changed_spi)} sd_changed={int(changed_sd)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
