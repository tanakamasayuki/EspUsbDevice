#!/usr/bin/env python3

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ARCHIVE = ROOT / "third_party" / "tinyusb" / "upstream" / "src"
BUILD = ROOT / "src"

SOURCE_FILES = {
    "tusb.c",
    "common/tusb_fifo.c",
    "device/usbd.c",
    "class/hid/hid_device.c",
    "class/cdc/cdc_device.c",
    "class/midi/midi_device.c",
    "class/msc/msc_device.c",
    "class/vendor/vendor_device.c",
    "class/net/ncm_device.c",
    "class/audio/audio_device.c",
    "portable/synopsys/dwc2/dcd_dwc2.c",
    "portable/synopsys/dwc2/dwc2_common.c",
}

UPSTREAM_DIRS = ("class", "common", "device", "host", "osal", "portable", "typec")


def build_upstream_files() -> set[str]:
    files = {"tusb.c", "tusb.h", "tusb_option.h"}
    for directory in UPSTREAM_DIRS:
        files.update(
            str(path.relative_to(BUILD))
            for path in (BUILD / directory).rglob("*")
            if path.is_file()
        )
    return files


def main() -> int:
    errors: list[str] = []
    build_files = build_upstream_files()
    actual_sources = {path for path in build_files if path.endswith(".c")}
    if actual_sources != SOURCE_FILES:
        errors.append(
            "compiled TinyUSB source set differs:\n"
            f"  missing: {sorted(SOURCE_FILES - actual_sources)}\n"
            f"  extra: {sorted(actual_sources - SOURCE_FILES)}"
        )

    for relative in sorted(build_files):
        archived = ARCHIVE / relative
        built = BUILD / relative
        if not archived.is_file():
            errors.append(f"build-only upstream file: {relative}")
        elif archived.read_bytes() != built.read_bytes():
            errors.append(f"modified upstream file: {relative}")

    if errors:
        print("\n".join(errors))
        return 1

    print(
        f"TinyUSB vendor tree verified: {len(build_files)} files, "
        f"{len(SOURCE_FILES)} compiled sources"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
