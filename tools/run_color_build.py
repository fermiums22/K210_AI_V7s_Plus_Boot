#!/usr/bin/env python3
import ctypes
import os
import re
import subprocess
import sys

ENABLE_VIRTUAL_TERMINAL_PROCESSING = 0x0004
STD_OUTPUT_HANDLE = -11

RESET = "\033[0m"
DIM = "\033[2m"
GREEN = "\033[32m"
YELLOW = "\033[33m"
RED = "\033[31m"
CYAN = "\033[36m"
BOLD = "\033[1m"

_PERCENT_RE = re.compile(r"(\[\s*\d+%\])")
_ERROR_RE = re.compile(r"\b(error|fatal|undefined reference|failed)\b", re.IGNORECASE)
_WARN_RE = re.compile(r"\b(warning|deprecated)\b", re.IGNORECASE)
_OK_RE = re.compile(r"\b(OK|Built target|ready)\b")


def enable_vt() -> bool:
    if os.name != "nt":
        return True
    try:
        kernel32 = ctypes.windll.kernel32
        handle = kernel32.GetStdHandle(STD_OUTPUT_HANDLE)
        mode = ctypes.c_uint32()
        if not kernel32.GetConsoleMode(handle, ctypes.byref(mode)):
            return False
        return bool(kernel32.SetConsoleMode(handle, mode.value | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
    except Exception:
        return False


def colorize(line: str, colors: bool) -> str:
    if not colors:
        return line

    stripped = line.rstrip("\r\n")
    ending = line[len(stripped):]

    if _ERROR_RE.search(stripped):
        return f"{RED}{BOLD}{stripped}{RESET}{ending}"
    if _WARN_RE.search(stripped):
        return f"{YELLOW}{stripped}{RESET}{ending}"

    stripped = _PERCENT_RE.sub(lambda m: f"{CYAN}{BOLD}{m.group(1)}{RESET}", stripped)

    if _OK_RE.search(stripped):
        stripped = f"{GREEN}{stripped}{RESET}"
    elif "Entering directory" in stripped or "Leaving directory" in stripped:
        stripped = f"{DIM}{stripped}{RESET}"

    return stripped + ending


def main() -> int:
    args = sys.argv[1:]
    if args and args[0] == "--":
        args = args[1:]
    if not args:
        print("usage: run_color_build.py -- <command> [args...]", file=sys.stderr)
        return 2

    colors = os.environ.get("BOOT_BUILD_COLOR", "1") != "0" and os.environ.get("NO_COLOR", "") == ""
    colors = colors and enable_vt()

    proc = subprocess.Popen(
        args,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        bufsize=1,
    )

    assert proc.stdout is not None
    for line in proc.stdout:
        sys.stdout.write(colorize(line, colors))
        sys.stdout.flush()

    return proc.wait()


if __name__ == "__main__":
    raise SystemExit(main())
