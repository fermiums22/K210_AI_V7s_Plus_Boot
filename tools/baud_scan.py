#!/usr/bin/env python3
from __future__ import annotations

import argparse
import time
import serial

BAUDS_DEFAULT = [1500000, 1000000, 921600, 460800, 230400, 115200, 57600, 38400]


def open_no_reset(port: str, baud: int) -> serial.Serial:
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = baud
    ser.timeout = 0.05
    ser.rtscts = False
    ser.dsrdtr = False
    ser.xonxoff = False
    ser.dtr = False
    ser.rts = False
    ser.open()
    ser.setDTR(False)
    ser.setRTS(False)
    return ser


def printable_ratio(data: bytes) -> float:
    if not data:
        return 0.0
    ok = 0
    for b in data:
        if b in (9, 10, 13) or 32 <= b <= 126:
            ok += 1
    return ok / len(data)


def clean_sample(data: bytes, limit: int = 240) -> str:
    s = data[:limit].decode("utf-8", errors="replace")
    return s.replace("\r", "\\r").replace("\n", "\\n")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--seconds", type=float, default=4.0)
    ap.add_argument("--bauds", default=",".join(str(b) for b in BAUDS_DEFAULT))
    args = ap.parse_args()

    bauds = [int(x.strip()) for x in args.bauds.split(",") if x.strip()]
    print(f"baud scan {args.port}; {args.seconds:g}s per baud; dtr=0 rts=0")
    for baud in bauds:
        data = bytearray()
        print(f"\n--- BAUD {baud} ---", flush=True)
        try:
            with open_no_reset(args.port, baud) as ser:
                deadline = time.monotonic() + args.seconds
                while time.monotonic() < deadline:
                    chunk = ser.read(512)
                    if chunk:
                        data.extend(chunk)
        except Exception as e:
            print(f"ERROR open/read {baud}: {e}")
            continue

        ratio = printable_ratio(data)
        print(f"bytes={len(data)} printable={ratio:.2f}")
        if data:
            print(clean_sample(bytes(data)))
        else:
            print("<no data>")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
