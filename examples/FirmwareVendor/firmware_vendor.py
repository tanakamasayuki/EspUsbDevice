#!/usr/bin/env python3
"""Send a firmware image to the FirmwareVendor sketch.

    uv run --with pyusb python3 firmware_vendor.py build/FirmwareVendor.ino.bin

On Linux this needs write access to the device node - a udev rule for the
device's VID:PID, or sudo. On Windows the vendor interface needs the WinUSB
driver, which the sketch's Microsoft OS 2.0 descriptor asks for automatically.
"""

import sys
import time

import usb.core
import usb.util

VID = 0x303A
PID = 0x4000

OUT = 0x40  # host->device, vendor, device recipient
IN = 0xC0   # device->host, vendor, device recipient

REQUEST_START, REQUEST_COMMIT, REQUEST_ABORT, REQUEST_STATUS = 1, 2, 3, 4


def find_bulk_out(device):
    for interface in device.get_active_configuration():
        if interface.bInterfaceClass != 0xFF:
            continue
        for endpoint in interface:
            is_bulk = usb.util.endpoint_type(endpoint.bmAttributes) == usb.util.ENDPOINT_TYPE_BULK
            is_out = usb.util.endpoint_direction(endpoint.bEndpointAddress) == usb.util.ENDPOINT_OUT
            if is_bulk and is_out:
                return interface.bInterfaceNumber, endpoint
    sys.exit("device has no vendor bulk OUT endpoint")


def status(device):
    data = device.ctrl_transfer(IN, REQUEST_STATUS, 0, 0, 8, timeout=5000)
    active = data[0]
    error = data[1]
    written = int.from_bytes(bytes(data[4:8]), "little")
    return active, error, written


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    image = open(sys.argv[1], "rb").read()
    if not image.startswith(b"\xe9"):
        sys.exit(f"{sys.argv[1]} does not start with 0xE9 - not an ESP application image")

    device = usb.core.find(idVendor=VID, idProduct=PID)
    if device is None:
        sys.exit(f"no device {VID:04x}:{PID:04x}")
    number, endpoint = find_bulk_out(device)
    usb.util.claim_interface(device, number)
    print(f"device  {device.product} / {device.serial_number}")
    print(f"bulk    ep=0x{endpoint.bEndpointAddress:02x} mps={endpoint.wMaxPacketSize}")
    print(f"image   {sys.argv[1]} {len(image)} bytes")

    size = len(image)
    device.ctrl_transfer(OUT, REQUEST_START, size & 0xFFFF, (size >> 16) & 0xFFFF,
                         None, timeout=5000)
    time.sleep(0.05)
    active, error, _ = status(device)
    if not active:
        sys.exit(f"device refused to start (error {error})")

    started = time.time()
    # 16 KiB per transfer: the device drains its FIFO from loop(), so larger
    # transfers mean fewer round trips and no more risk.
    for offset in range(0, size, 16384):
        endpoint.write(image[offset:offset + 16384], timeout=10000)
        print(f"\r{min(offset + 16384, size)}/{size} bytes", end="", flush=True)
    print()

    # Wait for the device to finish writing what is still in its FIFO.
    deadline = time.time() + 30
    while time.time() < deadline:
        active, error, written = status(device)
        if error:
            sys.exit(f"device reported error {error} after {written} bytes")
        if written >= size:
            break
        time.sleep(0.05)
    else:
        sys.exit("device stopped accepting data")

    elapsed = time.time() - started
    print(f"sent    {size} bytes in {elapsed:.1f}s ({size / elapsed / 1024:.0f} KiB/s)")

    try:
        device.ctrl_transfer(OUT, REQUEST_COMMIT, 0, 0, None, timeout=5000)
    except usb.core.USBError as error:
        sys.exit(f"commit failed: {error}")
    print("commit  accepted; the board restarts into the image if it verified")


if __name__ == "__main__":
    main()
