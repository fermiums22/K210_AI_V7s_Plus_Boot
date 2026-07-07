#!/usr/bin/env python3
"""Legacy no-op.

The SPI3 command-mode read/write smoke support is now committed in normal
sources:
  - src/boot_spi3_rw.c
  - src/boot_cmd.c

This script is intentionally kept as a no-op so older local batch files that
still call it do not mutate the source tree before building.
"""


def main() -> int:
    print("BOOT_SPI3_API_PATCH_OK permanent=1 noop=1")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
