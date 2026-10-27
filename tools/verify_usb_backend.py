#!/usr/bin/env python3
"""Offline check: v3 builds its maintained native backend, with no vendor stack."""
from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"

def main():
    errors = []
    origin = json.loads((ROOT / "third_party/tinyusb/ORIGIN.json").read_text())
    if not re.fullmatch(r"[a-f0-9]{40}", origin["commit"]):
        errors.append("origin must record the full source SHA")
    for name in origin["maintained_files"]:
        if not (ROOT / name).is_file(): errors.append("missing maintained source: " + name)
    for name in ("class", "common", "device", "host", "osal", "portable", "typec"):
        for path in (SRC / name).rglob("*"):
            if path.is_file(): errors.append("restored vendor path: " + str(path.relative_to(ROOT)))
    for name in ("tusb.h", "tusb_option.h", "tusb_config.h"):
        if (SRC / name).exists(): errors.append("restored vendor configuration: " + name)
    include = re.compile(r'^\s*#\s*include\s*[<"](?:tusb[^/"<>]*|(?:class|common|device|host|osal|portable)/)', re.M)
    for path in SRC.rglob("*"):
        if path.suffix in (".h", ".c", ".cpp") and include.search(path.read_text()):
            errors.append("vendor include: " + str(path.relative_to(ROOT)))
    if errors:
        print("\n".join(errors)); return 1
    print("Native USB backend verified offline; no vendor build snapshot/configuration")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
