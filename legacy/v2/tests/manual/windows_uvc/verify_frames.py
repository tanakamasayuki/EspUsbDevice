#!/usr/bin/env python3
"""Check that decoded frames are the picture the device drew.

Byte-for-byte comparison against the frames the device sent proves the
transport. It does not prove the host can *decode* them, and it says nothing
about a device-side drawing bug: an earlier version of this sketch wrote YUY2
with V held at 128, which left the luma ramp perfect and every colour wrong,
and passed a luma-only check.

So this decodes to RGB with ffmpeg and checks the colours, then follows the
block that steps one cell per frame along the bottom, which is what separates a
live stream from a stalled one.

    # MJPEG (the default build)
    uv run --with pillow python verify_frames.py --format mjpeg
    # uncompressed (-DVAR_FORMAT=1)
    uv run --with pillow python verify_frames.py --format yuy2 --size 160x120

Run it from this directory on a WSL host whose Windows side can see the camera,
with the device NOT attached to WSL.
"""

from __future__ import annotations

import argparse
import glob
import shutil
import subprocess
import sys
from pathlib import Path

# The eight bars as a host should decode them. Wide tolerance: the path is
# YUV 4:2:2 to RGB, and MJPEG is lossy on top of that.
BARS = [
    (255, 255, 255), (255, 255, 0), (0, 255, 255), (0, 255, 0),
    (255, 0, 255), (255, 0, 0), (0, 0, 255), (0, 0, 0),
]
OUT = "C:\\Users\\Public\\uvc_verify"
OUT_WSL = "/mnt/c/Users/Public/uvc_verify"


def near(a, b, tol):
    return all(abs(x - y) <= tol for x, y in zip(a, b))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--format", choices=("mjpeg", "yuy2"), default="mjpeg")
    parser.add_argument("--size", default=None, help="WxH; defaults to 320x240 for mjpeg, 160x120 for yuy2")
    parser.add_argument("--fps", type=int, default=15)
    parser.add_argument("--seconds", type=int, default=6)
    parser.add_argument("--device", default="EspUsbDevice Camera")
    parser.add_argument("--tolerance", type=int, default=40)
    arguments = parser.parse_args()
    size = arguments.size or ("320x240" if arguments.format == "mjpeg" else "160x120")
    width, height = (int(v) for v in size.split("x"))

    shutil.rmtree(OUT_WSL, ignore_errors=True)
    Path(OUT_WSL).mkdir(parents=True, exist_ok=True)
    # Sample at a third of the frame rate: enough frames to see motion without
    # writing hundreds of files. The block then steps by 3 cells per sample.
    sample = max(1, arguments.fps // 3)
    selector = ["-vcodec", "mjpeg"] if arguments.format == "mjpeg" else ["-pixel_format", "yuyv422"]
    command = [
        "ffmpeg.exe", "-hide_banner", "-loglevel", "warning", "-f", "dshow",
        *selector, "-video_size", size, "-framerate", str(arguments.fps),
        "-i", f"video={arguments.device}", "-t", str(arguments.seconds),
        "-vf", f"fps={sample}", "-y", OUT + "\\f%03d.png",
    ]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stdout, result.stderr)
        return 1

    from PIL import Image

    files = sorted(glob.glob(OUT_WSL + "/f*.png"))
    if not files:
        print("ffmpeg produced no frames")
        return 1

    failures = []
    positions = []
    for path in files:
        image = Image.open(path).convert("RGB")
        if image.size != (width, height):
            failures.append(f"{Path(path).name}: size {image.size}")
            continue
        for index, expected in enumerate(BARS):
            got = image.getpixel((int((index + 0.5) * width / 8), height // 3))
            if not near(got, expected, arguments.tolerance):
                failures.append(f"{Path(path).name}: bar {index} decoded {got}, expected {expected}")
        row = height * 7 // 8
        bright = [
            cell
            for cell in range(16)
            if sum(image.getpixel((int((cell + 0.5) * width / 16), row))) > 600
        ]
        positions.append(bright[0] if len(bright) == 1 else bright)

    print(f"{len(files)} frames decoded at {width}x{height} ({arguments.format})")
    print(f"colour bars correct in every frame: {not failures}")
    for failure in failures[:5]:
        print("  " + failure)
    print(f"moving block per frame: {positions}")
    steps = [
        (positions[i + 1] - positions[i]) % 16
        for i in range(len(positions) - 1)
        if isinstance(positions[i], int) and isinstance(positions[i + 1], int)
    ]
    stalled = [s for s in steps if s == 0]
    print(f"steps between samples: {sorted(set(steps))}, stalls: {len(stalled)}")
    ok = not failures and steps and not stalled
    print("OK" if ok else "NG")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
