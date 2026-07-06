#!/usr/bin/env python3
"""Pack a K210 slot image for direct SPI3 quad DMA32 loading.

The current K210 SPI3 quad + DFS32 + DMA32 path stores each received 32-bit
frame into RAM with bytes reversed relative to the normal slot image.  This
script pre-reverses every 32-bit word in the flash slot image so that the
hardware DMA reversal restores the original byte order in RAM.

Input : normal slot0 image, 4-byte aligned or padded by this script.
Output: dma32-packed slot0 image to write at the same flash offset.
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

APP_MAGIC = 0x4B323130
APP_MAGIC_INV = APP_MAGIC ^ 0xFFFFFFFF


def u32le(buf: bytes, off: int) -> int:
    return struct.unpack_from('<I', buf, off)[0]


def pack_dma32(data: bytes) -> bytes:
    pad = (-len(data)) & 3
    if pad:
        data += b'\xff' * pad
    out = bytearray(len(data))
    for i in range(0, len(data), 4):
        out[i + 0] = data[i + 3]
        out[i + 1] = data[i + 2]
        out[i + 2] = data[i + 1]
        out[i + 3] = data[i + 0]
    return bytes(out)


def main() -> int:
    ap = argparse.ArgumentParser(description='Pack normal slot0 image for K210 direct quad DMA32 loader')
    ap.add_argument('input', type=Path, help='normal slot0 .bin')
    ap.add_argument('output', type=Path, nargs='?', help='output .dma32.bin')
    args = ap.parse_args()

    src = args.input
    dst = args.output or src.with_suffix(src.suffix + '.dma32.bin')
    data = src.read_bytes()
    packed = pack_dma32(data)
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(packed)

    if len(data) >= 8:
        magic = u32le(data, 0)
        inv = u32le(data, 4)
        print(f'INPUT_HDR magic=0x{magic:08x} inv=0x{inv:08x}')
        if magic != APP_MAGIC or inv != APP_MAGIC_INV:
            print('WARN: input header does not look like a normal slot0 image')

        # What bootloader DMA32 will reconstruct in RAM after reading packed flash.
        recon = pack_dma32(packed[:32])
        rmagic = u32le(recon, 0)
        rinv = u32le(recon, 4)
        print(f'RAM_AFTER_DMA32 magic=0x{rmagic:08x} inv=0x{rinv:08x}')

    print(f'PACK_DMA32_IN  {src} ({len(data)} bytes)')
    print(f'PACK_DMA32_OUT {dst} ({len(packed)} bytes)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
