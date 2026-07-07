#!/usr/bin/env python3
import sys
import time

try:
    import serial
except Exception as exc:
    print(f"ERROR: pyserial import failed: {exc}")
    print("Install/check: py -3 -m pip install pyserial")
    raise SystemExit(2)


def read_line(ser, deadline):
    buf = bytearray()
    while time.time() < deadline:
        b = ser.read(1)
        if not b:
            continue
        if b == b"\n":
            try:
                return buf.decode("utf-8", errors="replace").rstrip("\r")
            except Exception:
                return repr(bytes(buf))
        if b != b"\r":
            buf += b
    return None


def wait_for(ser, needles, timeout_s):
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        line = read_line(ser, deadline)
        if line is None:
            break
        print(line)
        if any(n in line for n in needles):
            return line
    return None


def send_cmd(ser, cmd, timeout_s=15):
    print(f">>> {cmd}")
    ser.write((cmd + "\n").encode("ascii"))
    ser.flush()
    deadline = time.time() + timeout_s
    lines = []
    while time.time() < deadline:
        line = read_line(ser, deadline)
        if line is None:
            break
        print(line)
        lines.append(line)
        if line == "KBOOT:CMD":
            return lines
        if line.startswith("KBOOT:TIMEOUT") or line.startswith("KBOOT:ERR"):
            return lines
    return lines


def any_line(lines, prefix):
    return any(line.startswith(prefix) for line in lines)


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "COM8"
    baud = int(sys.argv[2]) if len(sys.argv) > 2 else 115200
    timeout = int(sys.argv[3]) if len(sys.argv) > 3 else 45

    print("=== K210 boot command smoke ===")
    print(f"Port: {port}")
    print(f"Baud: {baud}")
    print(f"Timeout: {timeout}s")

    with serial.Serial(port, baudrate=baud, timeout=0.05) as ser:
        ser.dtr = False
        ser.rts = False
        time.sleep(0.2)
        ser.reset_input_buffer()

        print("Waiting for KBOOT/KSD READY...")
        if wait_for(ser, ["KBOOT:READY", "KSD:READY", "KBOOT:BOOTMODE"], timeout) is None:
            print("ERROR: boot command service did not become ready")
            return 1

        print(">>> KSD1")
        ser.write(b"KSD1\n")
        ser.flush()
        if wait_for(ser, ["KBOOT:HELLO"], 5) is None:
            print("ERROR: no KBOOT:HELLO")
            return 2
        if wait_for(ser, ["KBOOT:CMD"], 5) is None:
            print("ERROR: no KBOOT:CMD prompt")
            return 3

        ok = True
        lines = send_cmd(ser, "HELP", 5)
        ok &= any_line(lines, "KBOOT:HELP")

        lines = send_cmd(ser, "SPI3_ID", 10)
        ok &= any_line(lines, "KBOOT:SPI3_ID 0x") and not any("0xffffffff" in x for x in lines)

        lines = send_cmd(ser, "SPI3_READ 0x00000000 16", 10)
        ok &= any_line(lines, "KBOOT:SPI3_READ_OK")

        lines = send_cmd(ser, "SPI3_RW", 30)
        ok &= any_line(lines, "KBOOT:SPI3_RW_OK")

        send_cmd(ser, "DONE", 5)

    print("BOOT_CMD_SMOKE_PASS" if ok else "BOOT_CMD_SMOKE_FAIL")
    return 0 if ok else 10


if __name__ == "__main__":
    raise SystemExit(main())
