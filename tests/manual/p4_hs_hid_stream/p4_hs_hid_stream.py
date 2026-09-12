#!/usr/bin/env python3
"""Check an ESP32-P4 HID vendor function at the packet size high speed allows.

Three separate claims, checked separately, because two of them can look right
while the third is wrong:

1. the interrupt endpoint is 512 bytes in the high-speed configuration and 64 in
   the full-speed one, which is the only value USB 2.0 defines there;
2. the HID report descriptor declares the report size the endpoint carries -
   Report Count is what a host sizes its reads from, so a descriptor frozen at 63
   beside a 512-byte endpoint makes the host read the wrong number of bytes;
3. reports actually arrive, in order, at the rate the endpoint promises.

Run from ``tests``:

    uv run --with pyusb python manual/p4_hs_hid_stream/p4_hs_hid_stream.py
"""

from __future__ import annotations

import argparse
import sys
import time

try:
    import usb.core
    import usb.util
except ImportError:
    sys.exit("run this with `uv run --with pyusb`")

VID = 0x1209
PID = 0x0008
USB_SPEED_HIGH = 3
DESC_CONFIGURATION = 0x02
DESC_OTHER_SPEED = 0x07
DESC_HID_REPORT = 0x22


def get_descriptor(device, descriptor_type: int, index: int, length: int, interface: int = 0):
    recipient = 0x81 if descriptor_type == DESC_HID_REPORT else 0x80
    return device.ctrl_transfer(
        recipient, 0x06, (descriptor_type << 8) | index, interface, length
    )


def walk_endpoints(descriptor: bytes):
    offset = 0
    while offset + 1 < len(descriptor):
        length = descriptor[offset]
        if length < 2:
            break
        if descriptor[offset + 1] == 0x05:
            yield {
                "address": descriptor[offset + 2],
                "attributes": descriptor[offset + 3],
                "max_packet": descriptor[offset + 4] | (descriptor[offset + 5] << 8),
                "interval": descriptor[offset + 6],
            }
        offset += length


def report_count(report_descriptor: bytes):
    """The Report Count item, in either its one- or two-byte form."""
    offset = 0
    while offset < len(report_descriptor):
        prefix = report_descriptor[offset]
        size = prefix & 0x03
        size = 4 if size == 3 else size
        tag = prefix & 0xFC
        if tag == 0x94:  # Report Count, Global
            data = report_descriptor[offset + 1 : offset + 1 + size]
            return int.from_bytes(data, "little")
        offset += 1 + size
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reports", type=int, default=8000)
    arguments = parser.parse_args()

    device = usb.core.find(idVendor=VID, idProduct=PID)
    if device is None:
        sys.exit(f"No device {VID:04x}:{PID:04x}; flash the sketch and attach the OTG port")
    speed = getattr(device, "speed", None)
    print(
        f"device {VID:04x}:{PID:04x} speed={speed} "
        f"({'high' if speed == USB_SPEED_HIGH else 'NOT HIGH SPEED'})"
    )

    for interface_number in range(4):
        try:
            if device.is_kernel_driver_active(interface_number):
                device.detach_kernel_driver(interface_number)
        except (usb.core.USBError, NotImplementedError):
            pass

    header = get_descriptor(device, DESC_CONFIGURATION, 0, 9)
    total = header[2] | (header[3] << 8)
    configuration = bytes(get_descriptor(device, DESC_CONFIGURATION, 0, total))
    endpoints = list(walk_endpoints(configuration))
    print(f"current-speed configuration ({total} bytes):")
    for endpoint in endpoints:
        print(
            f"  ep 0x{endpoint['address']:02x} attr=0x{endpoint['attributes']:02x} "
            f"mps={endpoint['max_packet']} bInterval={endpoint['interval']}"
        )

    try:
        other_header = get_descriptor(device, DESC_OTHER_SPEED, 0, 9)
        other_total = other_header[2] | (other_header[3] << 8)
        other = bytes(get_descriptor(device, DESC_OTHER_SPEED, 0, other_total))
        print(f"other-speed configuration ({other_total} bytes):")
        for endpoint in walk_endpoints(other):
            print(
                f"  ep 0x{endpoint['address']:02x} attr=0x{endpoint['attributes']:02x} "
                f"mps={endpoint['max_packet']} bInterval={endpoint['interval']}"
            )
    except usb.core.USBError as error:
        print(f"other-speed configuration: not available ({error})")

    report_descriptor = bytes(get_descriptor(device, DESC_HID_REPORT, 0, 64))
    count = report_count(report_descriptor)
    print(
        f"HID report descriptor: {len(report_descriptor)} bytes, Report Count = {count}"
    )

    endpoint_in = next(
        (e for e in endpoints if e["address"] & 0x80 and (e["attributes"] & 0x03) == 0x03),
        None,
    )
    if endpoint_in is None:
        sys.exit("no interrupt IN endpoint")

    usb.util.claim_interface(device, 0)
    size = endpoint_in["max_packet"]
    received = 0
    dropped = 0
    previous = None
    lengths = set()
    started = time.perf_counter()
    while received < arguments.reports:
        try:
            data = device.read(endpoint_in["address"], size, 2000)
        except usb.core.USBError as error:
            print(f"read error after {received} reports: {error}")
            break
        lengths.add(len(data))
        # Byte 0 is the report ID the device prepends; the sequence follows.
        sequence = int.from_bytes(bytes(data[1:5]), "little")
        if previous is not None and sequence != previous + 1:
            dropped += sequence - previous - 1
        previous = sequence
        received += 1
    elapsed = time.perf_counter() - started
    usb.util.dispose_resources(device)

    payload = max(lengths) if lengths else 0
    print(
        f"\n{received} reports in {elapsed:.3f}s = {received / elapsed:.0f} reports/s, "
        f"{received * payload / elapsed / 1e6:.2f} MB/s"
    )
    print(f"report lengths seen: {sorted(lengths)}   gaps in sequence: {dropped}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
