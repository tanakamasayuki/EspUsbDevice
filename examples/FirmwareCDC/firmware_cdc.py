#!/usr/bin/env python3
"""Send a firmware image to the FirmwareCDC sketch.

    python3 firmware_cdc.py /dev/ttyACM0 build/FirmwareCDC.ino.bin

Needs pyserial. Without installing anything:

    uv run --with pyserial python3 firmware_cdc.py /dev/ttyACM0 firmware.bin
"""

import sys
import time

import serial


def read_line(port, timeout=10.0):
    deadline = time.time() + timeout
    line = b""
    while time.time() < deadline:
        byte = port.read(1)
        if not byte:
            continue
        if byte in b"\r\n":
            if line:
                return line.decode("utf-8", "replace")
            continue
        line += byte
    raise TimeoutError("no reply from the device")


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    path, image_path = sys.argv[1], sys.argv[2]
    image = open(image_path, "rb").read()
    if not image.startswith(b"\xe9"):
        sys.exit(f"{image_path} does not start with 0xE9 - not an ESP application image")

    port = serial.Serial(path, 115200, timeout=0.2)
    port.reset_input_buffer()

    port.write(f"FW {len(image)}\n".encode())
    port.flush()
    reply = read_line(port)
    if reply != "READY":
        sys.exit(f"device refused: {reply}")

    started = time.time()
    sent = 0
    # 4 KiB at a time: large enough that the device is never starved, small
    # enough that a failure is reported before much more is on the wire.
    for offset in range(0, len(image), 4096):
        chunk = image[offset:offset + 4096]
        port.write(chunk)
        sent += len(chunk)
        print(f"\r{sent}/{len(image)} bytes", end="", flush=True)
    port.flush()
    print()

    reply = read_line(port, timeout=30.0)
    elapsed = time.time() - started
    if reply != "OK":
        sys.exit(f"device refused the image: {reply}")
    print(f"accepted {len(image)} bytes in {elapsed:.1f}s "
          f"({len(image) / elapsed / 1024:.0f} KiB/s); the board is restarting into it")


if __name__ == "__main__":
    main()
