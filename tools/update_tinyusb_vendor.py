#!/usr/bin/env python3
"""Preview or apply the pinned TinyUSB files selected by BUILD_FILES.txt."""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path

import verify_tinyusb_vendor as vendor


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--apply",
        action="store_true",
        help="copy changed manifest files from the ignored upstream cache to src/",
    )
    parser.add_argument(
        "--refresh",
        action="store_true",
        help="discard and download the cache for the configured commit again",
    )
    return parser.parse_args()


def changed_files(paths: set[str]) -> list[str]:
    changed = []
    for relative in sorted(paths):
        upstream = vendor.ARCHIVE / relative
        destination = vendor.BUILD / relative
        if not destination.is_file() or upstream.read_bytes() != destination.read_bytes():
            changed.append(relative)
    return changed


def main() -> int:
    args = parse_args()
    try:
        vendor.validate_metadata()
        expected = vendor.manifest_files()
        build_files = vendor.build_upstream_files()
        if build_files != expected:
            raise ValueError(
                "src/ and BUILD_FILES.txt differ; reconcile the manifest before "
                "updating the pinned source"
            )
        if args.refresh and vendor.CACHE.exists():
            shutil.rmtree(vendor.CACHE)
        vendor.populate_upstream_cache(expected)
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"TinyUSB update failed: {exc}")
        return 1

    upstream_version = vendor.tinyusb_version(vendor.ARCHIVE / "tusb_option.h")
    configured_version = vendor.UPSTREAM.get("tinyusb_version")
    if configured_version != upstream_version:
        print(
            "TinyUSB update refused: UPSTREAM.json tinyusb_version "
            f"{configured_version!r} does not match upstream {upstream_version!r}"
        )
        return 1

    changed = changed_files(expected)
    print(
        f"TinyUSB {vendor.UPSTREAM_COMMIT[:12]} ({upstream_version}): "
        f"{len(changed)} of {len(expected)} selected files differ"
    )
    for relative in changed:
        print(f"  {relative}")

    if not args.apply:
        if changed:
            print("Dry run only. Review the pin, then rerun with --apply.")
        return 0

    for relative in changed:
        source = vendor.ARCHIVE / relative
        destination = vendor.BUILD / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)

    remaining = changed_files(expected)
    if remaining:
        print(f"TinyUSB update failed: {len(remaining)} files still differ")
        return 1

    if changed:
        print(f"Updated {len(changed)} selected TinyUSB files.")
    else:
        print("The selected TinyUSB files were already current.")
    print("Next required step: regenerate/confirm dependencies, then run the full")
    print("S2/S3/P4 compile matrix and hardware pytest suite documented in")
    print("third_party/tinyusb/PROVENANCE.ja.md.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
