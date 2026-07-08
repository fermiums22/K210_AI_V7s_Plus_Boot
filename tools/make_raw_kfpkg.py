#!/usr/bin/env python3
import shutil
import sys
import zipfile
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 4:
        print("Usage: make_raw_kfpkg.py LOCAL_FILE FLASH_OFF OUT_KFPKG")
        return 2

    src = Path(sys.argv[1]).resolve()
    flash_off = sys.argv[2]
    out = Path(sys.argv[3]).resolve()
    if not src.is_file():
        print(f"ERROR: file not found: {src}")
        return 2

    work = out.with_suffix("")
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)

    payload = work / "payload.bin"
    shutil.copyfile(src, payload)
    (work / "flash-list.json").write_text(
        '{\n'
        '  "version": "0.1.0",\n'
        '  "files": [\n'
        '    {\n'
        f'      "address": {flash_off},\n'
        '      "bin": "payload.bin",\n'
        '      "sha256Prefix": false\n'
        '    }\n'
        '  ]\n'
        '}\n',
        encoding="utf-8")

    if out.exists():
        out.unlink()
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.write(work / "flash-list.json", "flash-list.json")
        zf.write(payload, "payload.bin")
    shutil.rmtree(work)
    print(out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
