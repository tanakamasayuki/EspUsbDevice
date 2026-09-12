#!/usr/bin/env python3
"""Measure one-way high-speed bulk IN from a P4 running p4_hs_stream.ino.

Shaped to be comparable with the E069-E071 experiments that asked for these
knobs: 4 MiB per run, a 1 MiB host read size, the pattern verified so a fast run
that lost data cannot look good, and the device's own stall count read back over
the console rather than inferred.

Run from ``tests``:

    uv run --with pyusb python manual/p4_hs_stream/p4_hs_stream.py --runs 9

On WSL the OTG port has to be attached first:

    usbipd.exe attach --wsl --busid <n>
"""

from __future__ import annotations

import argparse
import array
import statistics
import sys
import time

try:
    import usb.core
    import usb.util
except ImportError:
    sys.exit("run this with `uv run --with pyusb`")

VID = 0x1209
PID = 0x0008
TRANSFER_BYTES = 4 * 1024 * 1024
PATTERN_BYTES = 64 * 1024
USB_SPEED_HIGH = 3


def expected_words() -> array.array:
    words = PATTERN_BYTES // 4
    return array.array("I", [index % words for index in range(TRANSFER_BYTES // 4)])


def open_device():
    device = usb.core.find(idVendor=VID, idProduct=PID)
    if device is None:
        sys.exit(
            f"No device {VID:04x}:{PID:04x}. Flash tests/manual/p4_hs_stream and, "
            "on WSL, attach the OTG port with `usbipd.exe attach --wsl --busid <n>`."
        )
    try:
        device.get_active_configuration()
    except usb.core.USBError:
        device.set_configuration()
    usb.util.claim_interface(device, 0)
    return device


def bulk_endpoints(device):
    configuration = device.get_active_configuration()
    for interface in configuration:
        if interface.bInterfaceClass != 0xFF:
            continue
        endpoint_in = endpoint_out = None
        for endpoint in interface:
            if usb.util.endpoint_type(endpoint.bmAttributes) != usb.util.ENDPOINT_TYPE_BULK:
                continue
            if usb.util.endpoint_direction(endpoint.bEndpointAddress) == usb.util.ENDPOINT_IN:
                endpoint_in = endpoint
            else:
                endpoint_out = endpoint
        if endpoint_in is not None and endpoint_out is not None:
            return endpoint_in, endpoint_out
    sys.exit("no vendor bulk IN/OUT pair")


def one_run(device, endpoint_in, endpoint_out, read_size: int, expected: array.array) -> dict:
    # Leave nothing from a previous round in the pipe.
    try:
        while len(device.read(endpoint_in.bEndpointAddress, 65536, 30)):
            pass
    except usb.core.USBError:
        pass

    device.write(endpoint_out.bEndpointAddress, b"S", 1000)

    received = bytearray()
    overflow = b""
    error = None
    short_reads = 0
    started = time.perf_counter()
    while len(received) < TRANSFER_BYTES:
        want = min(read_size, TRANSFER_BYTES - len(received))
        try:
            block = device.read(endpoint_in.bEndpointAddress, want, 8000)
        except usb.core.USBError as exc:
            error = f"read error at {len(received)}: {exc}"
            break
        if not len(block):
            error = f"empty read at {len(received)}"
            break
        if len(block) < want:
            # A URB that came back short, which on this path means the device ran
            # the FIFO dry and TinyUSB terminated the transfer with a ZLP.
            short_reads += 1
        received.extend(block)
    elapsed = time.perf_counter() - started
    if len(received) > TRANSFER_BYTES:
        # The trailing stats packet can share a URB with the tail of the stream.
        overflow = bytes(received[TRANSFER_BYTES:])
        del received[TRANSFER_BYTES:]

    mismatch_at = None
    if error is None:
        got = array.array("I")
        got.frombytes(bytes(received))
        if got != expected:
            for index, (a, b) in enumerate(zip(got, expected)):
                if a != b:
                    mismatch_at = index * 4
                    break

    stats = overflow
    deadline = time.time() + 3
    while b"SEND" not in stats and time.time() < deadline:
        try:
            stats += bytes(device.read(endpoint_in.bEndpointAddress, read_size, 500))
        except usb.core.USBError:
            break
    device_line = stats.decode("ascii", "replace").strip()

    return {
        "received": len(received),
        "elapsed_s": elapsed,
        "rate_mb_s": (len(received) / elapsed / 1e6) if elapsed > 0 else 0.0,
        "short_reads": short_reads,
        "mismatch_at": mismatch_at,
        "error": error,
        "device": device_line,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runs", type=int, default=9)
    parser.add_argument("--read-size", type=int, default=1024 * 1024)
    parser.add_argument("--label", default="")
    arguments = parser.parse_args()

    device = open_device()
    endpoint_in, endpoint_out = bulk_endpoints(device)
    speed = getattr(device, "speed", None)
    print(
        f"{arguments.label} device {VID:04x}:{PID:04x} speed={speed} "
        f"({'high' if speed == USB_SPEED_HIGH else 'NOT HIGH SPEED'}) "
        f"ep_in=0x{endpoint_in.bEndpointAddress:02x} mps={endpoint_in.wMaxPacketSize}"
    )

    expected = expected_words()
    rows = []
    try:
        for _ in range(arguments.runs):
            rows.append(
                one_run(device, endpoint_in, endpoint_out, arguments.read_size, expected)
            )
    finally:
        usb.util.dispose_resources(device)

    for index, row in enumerate(rows, 1):
        print(
            f"run {index}: {row['rate_mb_s']:6.2f} MB/s  short_reads={row['short_reads']:<5} "
            f"mismatch={row['mismatch_at']}  err={row['error']}  {row['device']}"
        )

    rates = [row["rate_mb_s"] for row in rows if row["error"] is None]
    mismatches = [row for row in rows if row["mismatch_at"] is not None]
    if rates:
        print(
            f"\n{arguments.label} median {statistics.median(rates):.2f} MB/s   "
            f"min {min(rates):.2f}   max {max(rates):.2f}   n={len(rates)}   "
            f"mismatches={len(mismatches)}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
