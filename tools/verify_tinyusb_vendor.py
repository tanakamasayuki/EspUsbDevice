#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import re
import shutil
import tarfile
import urllib.error
import urllib.request
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
METADATA = ROOT / "third_party" / "tinyusb" / "UPSTREAM.json"
UPSTREAM = json.loads(METADATA.read_text(encoding="utf-8"))
UPSTREAM_COMMIT = UPSTREAM["commit"]
UPSTREAM_REPOSITORY = UPSTREAM["repository"]
UPSTREAM_URL = (
    f"https://codeload.github.com/{UPSTREAM_REPOSITORY}/tar.gz/"
    + UPSTREAM_COMMIT
)
CACHE = ROOT / "third_party" / "tinyusb" / ".upstream-cache" / UPSTREAM_COMMIT
ARCHIVE = CACHE / "src"
TARBALL = CACHE / "tinyusb.tar.gz"
MANIFEST = ROOT / "third_party" / "tinyusb" / "BUILD_FILES.txt"
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


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Compare the selected TinyUSB build files with the pinned upstream "
            "commit. Upstream is downloaded only when the ignored cache is absent."
        )
    )
    parser.add_argument(
        "--refresh",
        action="store_true",
        help="discard and download the ignored upstream verification cache again",
    )
    return parser.parse_args()


def build_upstream_files() -> set[str]:
    files = {"tusb.c", "tusb.h", "tusb_option.h"}
    for directory in UPSTREAM_DIRS:
        files.update(
            str(path.relative_to(BUILD))
            for path in (BUILD / directory).rglob("*")
            if path.is_file()
        )
    return files


def manifest_files() -> set[str]:
    entries = [
        line.strip()
        for line in MANIFEST.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    ]
    if len(entries) != len(set(entries)):
        raise ValueError(f"duplicate entry in {MANIFEST}")
    return set(entries)


def validate_metadata() -> None:
    if UPSTREAM.get("schema") != 1:
        raise ValueError(f"unsupported schema in {METADATA}")
    if not re.fullmatch(r"[0-9a-f]{40}", UPSTREAM_COMMIT):
        raise ValueError(f"commit in {METADATA} must be a full lowercase SHA")
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", UPSTREAM_REPOSITORY):
        raise ValueError(f"repository in {METADATA} must use owner/name form")


def tinyusb_version(path: Path) -> str:
    content = path.read_text(encoding="utf-8")
    values = []
    for macro in ("MAJOR", "MINOR", "REVISION"):
        match = re.search(
            rf"^#define\s+TUSB_VERSION_{macro}\s+(\d+)\s*$",
            content,
            re.MULTILINE,
        )
        if not match:
            raise ValueError(f"TUSB_VERSION_{macro} not found in {path}")
        values.append(match.group(1))
    return ".".join(values)


def download_tarball() -> None:
    CACHE.mkdir(parents=True, exist_ok=True)
    temporary = TARBALL.with_suffix(".tmp")
    print(f"Downloading TinyUSB {UPSTREAM_COMMIT} for verification...")
    try:
        with urllib.request.urlopen(UPSTREAM_URL, timeout=60) as response:
            with temporary.open("wb") as output:
                shutil.copyfileobj(response, output)
    except (OSError, urllib.error.URLError) as exc:
        temporary.unlink(missing_ok=True)
        raise RuntimeError(f"failed to download {UPSTREAM_URL}: {exc}") from exc
    temporary.replace(TARBALL)


def populate_upstream_cache(expected_files: set[str]) -> None:
    if all((ARCHIVE / relative).is_file() for relative in expected_files):
        return
    if not TARBALL.is_file():
        download_tarball()

    repository_name = UPSTREAM_REPOSITORY.rsplit("/", 1)[-1]
    prefix = f"{repository_name}-{UPSTREAM_COMMIT}/src/"
    try:
        with tarfile.open(TARBALL, "r:gz") as archive:
            members = {member.name: member for member in archive.getmembers()}
            for relative in sorted(expected_files):
                member = members.get(prefix + relative)
                if member is None or not member.isfile():
                    raise RuntimeError(
                        f"pinned TinyUSB archive has no regular file: src/{relative}"
                    )
                source = archive.extractfile(member)
                if source is None:
                    raise RuntimeError(
                        f"cannot read pinned TinyUSB file: src/{relative}"
                    )
                destination = ARCHIVE / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(source.read())
    except (OSError, tarfile.TarError) as exc:
        raise RuntimeError(f"cannot read cached archive {TARBALL}: {exc}") from exc


def main() -> int:
    args = parse_args()
    try:
        validate_metadata()
    except ValueError as exc:
        print(exc)
        return 1
    if args.refresh and CACHE.exists():
        shutil.rmtree(CACHE)

    errors: list[str] = []
    build_files = build_upstream_files()
    expected_files = manifest_files()
    if build_files != expected_files:
        errors.append(
            "Arduino build tree differs from BUILD_FILES.txt:\n"
            f"  missing: {sorted(expected_files - build_files)}\n"
            f"  extra: {sorted(build_files - expected_files)}"
        )

    actual_sources = {path for path in build_files if path.endswith(".c")}
    if actual_sources != SOURCE_FILES:
        errors.append(
            "compiled TinyUSB source set differs:\n"
            f"  missing: {sorted(SOURCE_FILES - actual_sources)}\n"
            f"  extra: {sorted(actual_sources - SOURCE_FILES)}"
        )

    try:
        populate_upstream_cache(expected_files)
    except RuntimeError as exc:
        print(exc)
        return 1

    expected_version = UPSTREAM.get("tinyusb_version")
    try:
        upstream_version = tinyusb_version(ARCHIVE / "tusb_option.h")
        if expected_version != upstream_version:
            errors.append(
                f"UPSTREAM.json tinyusb_version is {expected_version!r}, "
                f"but pinned upstream reports {upstream_version!r}"
            )
    except ValueError as exc:
        errors.append(str(exc))

    for relative in sorted(build_files):
        archived = ARCHIVE / relative
        built = BUILD / relative
        if not archived.is_file():
            errors.append(f"build-only upstream file: {relative}")
        elif archived.read_bytes() != built.read_bytes():
            errors.append(
                f"modified upstream file: {relative} "
                "(use --refresh if the local verification cache is suspect)"
            )

    if errors:
        print("\n".join(errors))
        return 1

    print(
        f"TinyUSB vendor tree verified: {len(build_files)} selected files, "
        f"{len(SOURCE_FILES)} compiled sources, upstream {UPSTREAM_COMMIT[:12]}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
