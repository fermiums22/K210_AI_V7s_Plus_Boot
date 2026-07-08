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
        waiting = getattr(ser, "in_waiting", 0)
        if not waiting:
            time.sleep(0.01)
            continue
        b = ser.read(1)
        if not b:
            continue
        if b == b"\n":
            return buf.decode("utf-8", errors="replace").rstrip("\r")
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


def send_cmd(ser, cmd, timeout_s=20):
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
        if line.startswith("KBOOT:TIMEOUT"):
            return lines
    return lines


def send_done(ser, timeout_s=3):
    print(">>> DONE")
    ser.write(b"DONE\n")
    ser.flush()
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        line = read_line(ser, deadline)
        if line is None:
            break
        print(line)
        if line == "KBOOT:DONE":
            return


def command_ok(cmd, lines):
    if any(line.startswith("KBOOT:ERR") for line in lines):
        return False

    name = cmd.split()[0].upper() if cmd.split() else ""
    if name == "SD_MOUNT":
        return any(line == "KBOOT:SD_MOUNT_RESULT ok=1" for line in lines)
    if name == "SD_READ":
        return any(line == "KBOOT:SD_READ_OK" for line in lines)
    if name == "SD_FLASH":
        return any(line.startswith("KBOOT:SD_FLASH_OK") for line in lines)
    if name == "SPI3_ID":
        return any(line.startswith("KBOOT:SPI3_ID 0x") and not line.endswith("ffffffff") for line in lines)
    if name == "SPI3_RW":
        return any(line.startswith("KBOOT:SPI3_RW_OK") for line in lines)
    if name == "SD_TEST":
        return any(line == "KBOOT:SD_OK rw-64" for line in lines)
    if name == "HELP":
        return any(line.startswith("KBOOT:HELP") for line in lines)

    return not any("FAIL" in line for line in lines)


def command_timeout(cmd, base_timeout):
    name = cmd.split()[0].upper() if cmd.split() else ""
    if name == "SD_FLASH":
        return max(base_timeout, 240)
    return max(30, min(base_timeout, 60))


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "COM8"
    baud = int(sys.argv[2]) if len(sys.argv) > 2 else 115200
    timeout = int(sys.argv[3]) if len(sys.argv) > 3 else 45
    commands = sys.argv[4:] if len(sys.argv) > 4 else ["SD_MOUNT"]

    print("=== K210 boot SD debug ===")
    print(f"Port: {port}")
    print(f"Baud: {baud}")
    print(f"Timeout: {timeout}s")
    print("Commands:")
    for cmd in commands:
        print(f"  {cmd}")

    ok = True
    with serial.Serial(port, baudrate=baud, timeout=0, write_timeout=1) as ser:
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

        for cmd in commands:
            lines = send_cmd(ser, cmd, command_timeout(cmd, timeout))
            one_ok = command_ok(cmd, lines)
            ok &= one_ok
            print(f"CMD_RESULT {cmd}: {'PASS' if one_ok else 'FAIL'}")

        send_done(ser, 3)

    print("BOOT_SD_DEBUG_PASS" if ok else "BOOT_SD_DEBUG_FAIL")
    return 0 if ok else 20


if __name__ == "__main__":
    raise SystemExit(main())
