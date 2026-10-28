#!/usr/bin/env python3
"""Check a P4 High-Speed Device with PyUSB.

Run from ``tests``:

    uv run --with pyusb python manual/p4_hs_bulk/p4_hs_bulk.py --megabytes 16
"""

from __future__ import annotations

import argparse
import os
import sys
import time

try:
    import usb.core
    import usb.util
except ImportError:
    sys.exit("PyUSB is required: run this script with `uv run --with pyusb`")


VID = 0x303A
PID = 0x4041
USB_SPEED_HIGH = 3
USB_DT_DEVICE_QUALIFIER = 0x06
USB_DT_OTHER_SPEED_CONFIGURATION = 0x07
USB_ENDPOINT_XFER_BULK = 0x02
PACKET_SIZE = 512


def usb_device_node(device) -> str | None:
    bus = getattr(device, "bus", None)
    address = getattr(device, "address", None)
    if bus is None or address is None:
        return None
    return f"/dev/bus/usb/{bus:03d}/{address:03d}"


def exit_for_usb_error(device, operation: str, error) -> None:
    if getattr(error, "errno", None) == 13:
        node = usb_device_node(device)
        temporary_fix = (
            f"`sudo chmod a+rw {node}`"
            if node
            else "grant the current user read/write access to the USB device"
        )
        sys.exit(
            f"USB access denied while trying to {operation}.\n"
            f"Temporary check: {temporary_fix}, then rerun this command.\n"
            "Permanent Linux/WSL setup: install the 303a:4041 udev rule from "
            "tests/manual/README.md and reconnect or reattach the device."
        )
    sys.exit(f"USB error while trying to {operation}: {error}")


def get_descriptor(device, descriptor_type: int, length: int) -> bytes:
    return bytes(
        device.ctrl_transfer(
            0x80,
            0x06,  # GET_DESCRIPTOR
            descriptor_type << 8,
            0,
            length,
            timeout=2000,
        )
    )


def bulk_packet_sizes(raw_descriptor: bytes) -> list[int]:
    sizes: list[int] = []
    offset = 0
    while offset + 1 < len(raw_descriptor):
        length = raw_descriptor[offset]
        if length < 2 or offset + length > len(raw_descriptor):
            raise RuntimeError(f"malformed descriptor at offset {offset}")
        if raw_descriptor[offset + 1] == 0x05 and length >= 7:
            attributes = raw_descriptor[offset + 3] & 0x03
            if attributes == USB_ENDPOINT_XFER_BULK:
                sizes.append(
                    raw_descriptor[offset + 4]
                    | (raw_descriptor[offset + 5] << 8)
                )
        offset += length
    return sizes


def find_bulk_endpoints(configuration):
    interface = configuration[(0, 0)]
    bulk_out = usb.util.find_descriptor(
        interface,
        custom_match=lambda endpoint: (
            usb.util.endpoint_direction(endpoint.bEndpointAddress)
            == usb.util.ENDPOINT_OUT
            and usb.util.endpoint_type(endpoint.bmAttributes)
            == usb.util.ENDPOINT_TYPE_BULK
        ),
    )
    bulk_in = usb.util.find_descriptor(
        interface,
        custom_match=lambda endpoint: (
            usb.util.endpoint_direction(endpoint.bEndpointAddress)
            == usb.util.ENDPOINT_IN
            and usb.util.endpoint_type(endpoint.bmAttributes)
            == usb.util.ENDPOINT_TYPE_BULK
        ),
    )
    if bulk_out is None or bulk_in is None:
        raise RuntimeError("bulk IN/OUT endpoints were not found")
    return interface, bulk_out, bulk_in


def read_exact_bulk(endpoint, size: int, timeout_ms: int) -> tuple[bytes, int]:
    """Read one echo payload, ignoring legal ZLPs between full-size packets."""
    received = bytearray()
    zlp_count = 0
    consecutive_zlps = 0
    while len(received) < size:
        chunk = bytes(endpoint.read(size - len(received), timeout=timeout_ms))
        if not chunk:
            # TinyUSB terminates a flushed transfer whose payload is exactly one
            # endpoint MPS with a zero-length packet. libusb can deliver that ZLP
            # as the next read, before the following echo payload.
            zlp_count += 1
            consecutive_zlps += 1
            if consecutive_zlps > 2:
                raise RuntimeError(
                    f"received {consecutive_zlps} consecutive ZLPs while waiting for "
                    f"{size} echo bytes"
                )
            continue
        consecutive_zlps = 0
        received.extend(chunk)
    return bytes(received), zlp_count


def reset_configuration(device) -> None:
    """Reset class endpoints/FIFOs after a possibly interrupted previous run."""
    device.ctrl_transfer(
        0x00, 0x09, 0, 0, None, timeout=2000  # SET_CONFIGURATION(0)
    )
    time.sleep(0.05)
    device.ctrl_transfer(
        0x00, 0x09, 1, 0, None, timeout=2000  # SET_CONFIGURATION(1)
    )
    time.sleep(0.05)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check P4 HS descriptors and sustained raw bulk echo"
    )
    parser.add_argument(
        "--megabytes",
        type=int,
        default=16,
        help="amount to echo and verify (default: 16 MiB)",
    )
    parser.add_argument(
        "--timeout-ms",
        type=int,
        default=3000,
        help="timeout for each USB transfer (default: 3000 ms)",
    )
    args = parser.parse_args()
    if args.megabytes <= 0:
        parser.error("--megabytes must be positive")

    device = usb.core.find(idVendor=VID, idProduct=PID)
    if device is None:
        sys.exit(
            f"{VID:04x}:{PID:04x} not found; flash p4_hs_bulk.ino and connect "
            "the P4 HS Device connector to this PC"
        )

    speed = getattr(device, "speed", None)
    if speed != USB_SPEED_HIGH:
        sys.exit(f"expected a High-Speed link (speed=3), got speed={speed!r}")
    print("PASS link: USB High-Speed (480 Mbit/s signaling)", flush=True)

    try:
        reset_configuration(device)
    except usb.core.USBError as error:
        exit_for_usb_error(device, "reset the active configuration", error)
    configuration = device.get_active_configuration()
    interface, bulk_out, bulk_in = find_bulk_endpoints(configuration)
    if bulk_out.wMaxPacketSize != PACKET_SIZE or bulk_in.wMaxPacketSize != PACKET_SIZE:
        sys.exit(
            "expected active HS bulk MPS=512, got "
            f"OUT={bulk_out.wMaxPacketSize} IN={bulk_in.wMaxPacketSize}"
        )
    print(
        f"PASS active descriptor: OUT=0x{bulk_out.bEndpointAddress:02x} "
        f"IN=0x{bulk_in.bEndpointAddress:02x} bulk MPS=512"
    )

    qualifier = get_descriptor(device, USB_DT_DEVICE_QUALIFIER, 10)
    if len(qualifier) != 10 or qualifier[1] != USB_DT_DEVICE_QUALIFIER:
        sys.exit(f"invalid Device Qualifier descriptor: {qualifier.hex()}")
    print("PASS descriptor: Device Qualifier")

    other_header = get_descriptor(device, USB_DT_OTHER_SPEED_CONFIGURATION, 9)
    if len(other_header) < 4:
        sys.exit("Other-Speed Configuration header is too short")
    total_length = other_header[2] | (other_header[3] << 8)
    other = get_descriptor(
        device, USB_DT_OTHER_SPEED_CONFIGURATION, total_length
    )
    other_sizes = bulk_packet_sizes(other)
    if not other_sizes or any(size != 64 for size in other_sizes):
        sys.exit(
            f"expected other-speed bulk MPS=64, got {other_sizes or 'none'}"
        )
    print("PASS descriptor: Other-Speed Configuration bulk MPS=64")

    interface_number = interface.bInterfaceNumber
    detached = False
    if device.is_kernel_driver_active(interface_number):
        device.detach_kernel_driver(interface_number)
        detached = True
    usb.util.claim_interface(device, interface_number)

    total_bytes = args.megabytes * 1024 * 1024
    transferred = 0
    total_zlps = 0
    started = time.monotonic()
    try:
        while transferred < total_bytes:
            size = min(PACKET_SIZE, total_bytes - transferred)
            payload = os.urandom(size)
            written = bulk_out.write(payload, timeout=args.timeout_ms)
            if written != size:
                raise RuntimeError(
                    f"short OUT transfer at byte {transferred}: {written}/{size}"
                )
            echoed, zlp_count = read_exact_bulk(
                bulk_in, size, args.timeout_ms
            )
            total_zlps += zlp_count
            if echoed != payload:
                raise RuntimeError(
                    f"echo mismatch at byte {transferred}: "
                    f"sent={payload[:16].hex()} received={echoed[:16].hex()}"
                )
            transferred += size
    finally:
        usb.util.release_interface(device, interface_number)
        if detached:
            device.attach_kernel_driver(interface_number)
        usb.util.dispose_resources(device)

    elapsed = time.monotonic() - started
    mib_per_second = transferred / (1024 * 1024) / elapsed
    print(
        f"PASS bulk echo: {transferred / (1024 * 1024):.1f} MiB verified "
        f"in {elapsed:.1f}s ({mib_per_second:.2f} MiB/s, "
        f"ZLPs={total_zlps})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
