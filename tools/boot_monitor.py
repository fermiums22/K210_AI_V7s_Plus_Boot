#!/usr/bin/env python3
from __future__ import annotations

import argparse
import time

import serial


def open_no_reset(port: str, baud: int) -> serial.Serial:
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = baud
    ser.timeout = 0.2
    ser.rtscts = False
    ser.dsrdtr = False
    ser.xonxoff = False
    # Important for CH340/CH552 auto-ISP boards: do not assert modem-control
    # lines when the monitor opens.  KFlash uses these lines intentionally;
    # monitor must not.
    ser.dtr = False
    ser.rts = False
    ser.open()
    ser.setDTR(False)
    ser.setRTS(False)
    return ser


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--baud", type=int, default=921600)
    ap.add_argument("--seconds", type=float, default=15.0)
    args = ap.parse_args()

    deadline = time.monotonic() + args.seconds
    print(f"monitor open {args.port} {args.baud} for {args.seconds:g}s; dtr=0 rts=0")
    try:
        with open_no_reset(args.port, args.baud) as ser:
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
