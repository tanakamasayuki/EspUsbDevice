#!/usr/bin/env python3
"""Re-enumerate an EspUsbDevice board repeatedly and check it stays identical.

A device that enumerates once is not the same as a device that keeps
enumerating. This walks the board through many bus resets and configuration
changes and fails if anything drifts: descriptors that come back different, a
device that stops answering, or one that never comes back at all.

Two cycle kinds, because they exercise different code paths:

``config``
    ``SET_CONFIGURATION 0`` then ``SET_CONFIGURATION 1``. The device stays
    addressed; class endpoints are torn down and rebuilt, and
    ``onBusDetached()`` / ``onBusAttached()`` fire. This is what catches state a
    class kept across a deconfigure.

``reset``
    A real USB port reset. The device is re-addressed and re-enumerated from
    scratch, so the descriptors are rebuilt and sent again. This is what catches
    a descriptor buffer that is only correct the first time, or a controller
    that does not survive a reset.

Run from ``tests``:

    uv run --with pyusb python manual/enumeration_soak/enumeration_soak.py
    uv run --with pyusb python manual/enumeration_soak/enumeration_soak.py --mode reset --cycles 50
"""

from __future__ import annotations

import argparse
import sys
import time

try:
    import usb.core
    import usb.util
except ImportError:
    sys.exit("PyUSB is required: run this script with `uv run --with pyusb`")


DEFAULT_VID = 0x303A

DT_DEVICE = 0x01
DT_CONFIGURATION = 0x02


def find_device(vid: int, pid: int | None):
    finder = {"idVendor": vid}
    if pid is not None:
        finder["idProduct"] = pid
    return usb.core.find(**finder)


def wait_for_device(vid: int, pid: int | None, timeout_s: float):
    """Poll until the device is back on the bus, or give up.

    After a reset the host needs a moment to re-address and re-enumerate, and on
    Linux the udev/driver rebind adds more. Polling beats a fixed sleep because
    the delay varies by host and by hub.
    """
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        device = find_device(vid, pid)
        if device is not None:
            return device
        time.sleep(0.1)
    return None


def get_descriptor(device, descriptor_type: int, index: int, length: int) -> bytes:
    return bytes(
        device.ctrl_transfer(
            0x80,
            0x06,  # GET_DESCRIPTOR
            (descriptor_type << 8) | index,
            0,
            length,
            timeout=2000,
        )
    )


def read_snapshot(device) -> dict:
    """The bytes that must not change between cycles."""
    device_descriptor = get_descriptor(device, DT_DEVICE, 0, 18)
    header = get_descriptor(device, DT_CONFIGURATION, 0, 9)
    total = header[2] | (header[3] << 8)
    configuration = get_descriptor(device, DT_CONFIGURATION, 0, total)
    return {
        "device": device_descriptor,
        "configuration": configuration,
        "speed": getattr(device, "speed", None),
    }


def describe_difference(baseline: dict, current: dict) -> str | None:
    for key in ("device", "configuration"):
        if baseline[key] != current[key]:
            return (
                f"{key} descriptor changed\n"
                f"  first: {baseline[key].hex()}\n"
                f"  now:   {current[key].hex()}"
            )
    if baseline["speed"] != current["speed"]:
        return f"link speed changed: {baseline['speed']} -> {current['speed']}"
    return None


def cycle_configuration(device) -> None:
    device.ctrl_transfer(0x00, 0x09, 0, 0, None, timeout=2000)  # SET_CONFIGURATION 0
    time.sleep(0.05)
    device.ctrl_transfer(0x00, 0x09, 1, 0, None, timeout=2000)  # SET_CONFIGURATION 1
    time.sleep(0.05)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Re-enumerate an EspUsbDevice board repeatedly and diff the descriptors"
    )
    parser.add_argument("--vid", type=lambda v: int(v, 0), default=DEFAULT_VID)
    parser.add_argument("--pid", type=lambda v: int(v, 0), default=None)
    parser.add_argument("--cycles", type=int, default=20,
                        help="number of cycles to run (default: 20)")
    parser.add_argument("--mode", choices=("config", "reset", "both"), default="both",
                        help="cycle kind (default: both, alternating)")
    parser.add_argument("--settle-s", type=float, default=10.0,
                        help="how long to wait for the device to come back after a "
                             "reset (default: 10 s)")
    args = parser.parse_args()
    if args.cycles <= 0:
        parser.error("--cycles must be positive")

    device = find_device(args.vid, args.pid)
    if device is None:
        target = f"{args.vid:04x}:{args.pid:04x}" if args.pid is not None else f"{args.vid:04x}:*"
        sys.exit(
            f"no USB device matching {target} found. Flash an EspUsbDevice sketch "
            "and connect its device connector to this PC."
        )

    pid = device.idProduct
    print(f"target {device.idVendor:04x}:{pid:04x} speed={getattr(device, 'speed', None)}")

    try:
        baseline = read_snapshot(device)
    except usb.core.USBError as error:
        sys.exit(f"could not read the initial descriptors: {error}")
    print(
        f"baseline: device={len(baseline['device'])} bytes "
        f"configuration={len(baseline['configuration'])} bytes"
    )

    failures = 0
    durations: list[float] = []
    for cycle in range(1, args.cycles + 1):
        if args.mode == "both":
            kind = "config" if cycle % 2 else "reset"
        else:
            kind = args.mode

        started = time.monotonic()
        try:
            if kind == "config":
                cycle_configuration(device)
            else:
                device.reset()
                usb.util.dispose_resources(device)
                # After reset() the old handle is stale even when the device
                # keeps its address, so always re-find it.
                device = wait_for_device(args.vid, pid, args.settle_s)
                if device is None:
                    print(f"FAIL cycle {cycle} ({kind}): device did not come back "
                          f"within {args.settle_s}s")
                    failures += 1
                    break
            current = read_snapshot(device)
        except usb.core.USBError as error:
            print(f"FAIL cycle {cycle} ({kind}): {error}")
            failures += 1
            device = wait_for_device(args.vid, pid, args.settle_s)
            if device is None:
                print("       device is gone; stopping")
                break
            continue

        elapsed = time.monotonic() - started
        durations.append(elapsed)
        difference = describe_difference(baseline, current)
        if difference:
            print(f"FAIL cycle {cycle} ({kind}): {difference}")
            failures += 1
        else:
            print(f"ok   cycle {cycle:>4} ({kind:<6}) {elapsed * 1000:6.0f} ms")

    if device is not None:
        usb.util.dispose_resources(device)

    if durations:
        print(
            f"timing: min={min(durations) * 1000:.0f} ms "
            f"max={max(durations) * 1000:.0f} ms "
            f"mean={sum(durations) / len(durations) * 1000:.0f} ms"
        )
    if failures:
        print(f"FAIL {failures} of {args.cycles} cycles")
        return 1
    print(f"PASS {args.cycles} cycles, descriptors identical throughout")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
