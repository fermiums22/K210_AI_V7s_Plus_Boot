#!/usr/bin/env python3
from __future__ import annotations

import argparse
import time

import serial


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--baud", type=int, default=921600)
    ap.add_argument("--seconds", type=float, default=15.0)
    args = ap.parse_args()

    deadline = time.monotonic() + args.seconds
    print(f"monitor open {args.port} {args.baud} for {args.seconds:g}s")
    try:
        with serial.Serial(args.port, args.baud, timeout=0.2) as ser:
            while time.monotonic() < deadline:
                raw = ser.readline()
                if raw:
                    print(raw.decode("utf-8", errors="replace").rstrip(), flush=True)
    except KeyboardInterrupt:
        print("monitor interrupted")
        return 130
    print("monitor done")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
