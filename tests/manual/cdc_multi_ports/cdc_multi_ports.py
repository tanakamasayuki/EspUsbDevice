#!/usr/bin/env python3
"""Exercise every CDC serial port of a multi-port EspUsbDevice board from the PC.

The peer and loopback rigs can only drive the first CDC function, because
EspUsbHost binds one per device. A PC binds one driver per function, so it is
the host that can prove the rest: this opens every serial node the board
produced, round-trips a distinct probe on each, and checks the reply came back
on that same port and no other.

Run ``examples/SerialMulti`` on the board first - it echoes each line back with
the port's own name as a prefix, which is what lets this tell the ports apart.
Then connect the board's *device* connector to this PC.

Run from ``tests``:

    uv run --with pyserial python manual/cdc_multi_ports/cdc_multi_ports.py
    uv run --with pyserial python manual/cdc_multi_ports/cdc_multi_ports.py --pid 0x4018
    uv run --with pyserial python manual/cdc_multi_ports/cdc_multi_ports.py --expect 3
"""

from __future__ import annotations

import argparse
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial is required: run this script with `uv run --with pyserial`")


DEFAULT_VID = 0x303A
DEFAULT_PID = 0x4018


def find_ports(vid: int, pid: int, serial_number: str | None):
    """Serial nodes belonging to one board, in interface order.

    Ordering matters: the nth node in interface order is the nth CDC function in
    the configuration descriptor, which is the port index the device-side
    EspUsbDeviceCdcSerial::port() reports. Sorting by device name would order
    ttyACM10 before ttyACM9.
    """
    found = []
    for info in list_ports.comports():
        if info.vid != vid or info.pid != pid:
            continue
        if serial_number and info.serial_number != serial_number:
            continue
        found.append((interface_number(info), info))
    # A node whose interface could not be read sorts last rather than ahead of
    # everything, so a partial identification does not silently reorder ports.
    found.sort(key=lambda item: (item[0] < 0, item[0], item[1].device))
    return found


def interface_number(info) -> int:
    """bInterfaceNumber behind a serial node, or -1 when it cannot be read.

    A CDC ACM node is created for the *control* interface, so for a device whose
    ports are laid out control/data pairs these come out 0, 2, 4 - ascending in
    the same order as the functions appear in the configuration descriptor.
    """
    # Linux: location is "1-4:1.2", i.e. bus-port(.port):config.interface.
    if info.location and ":" in info.location:
        try:
            return int(info.location.split(":")[1].split(".")[1])
        except (IndexError, ValueError):
            pass
    # Windows: the hardware id carries "&MI_02" for the interface.
    hwid = (info.hwid or "").upper()
    marker = hwid.find("MI_")
    if marker >= 0:
        try:
            return int(hwid[marker + 3:marker + 5], 16)
        except ValueError:
            pass
    return -1


def probe(device: str, text: str, timeout: float) -> str:
    """Send one line and return what came back, or "" on timeout."""
    with serial.Serial(device, 115200, timeout=timeout) as port:
        # Opening asserts DTR, which the sketch reports; give the device a
        # moment before writing so the first bytes are not sent into a port the
        # host has only just configured.
        time.sleep(0.2)
        port.reset_input_buffer()
        port.write(text.encode() + b"\n")
        port.flush()
        deadline = time.time() + timeout
        received = b""
        while time.time() < deadline:
            chunk = port.read(64)
            if chunk:
                received += chunk
                if b"\n" in received:
                    break
            else:
                break
        return received.decode(errors="replace").strip()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vid", type=lambda v: int(v, 0), default=DEFAULT_VID)
    parser.add_argument("--pid", type=lambda v: int(v, 0), default=DEFAULT_PID)
    parser.add_argument("--serial", dest="serial_number", default=None,
                        help="iSerialNumber, to pick one of several identical boards")
    parser.add_argument("--expect", type=int, default=None,
                        help="fail unless exactly this many ports are found "
                             "(SerialMulti presents 2 on S2/S3, 3 on P4)")
    parser.add_argument("--timeout", type=float, default=2.0)
    args = parser.parse_args()

    ports = find_ports(args.vid, args.pid, args.serial_number)
    if not ports:
        print(f"no serial ports for {args.vid:04x}:{args.pid:04x}", file=sys.stderr)
        return 1

    print(f"{len(ports)} port(s) for {args.vid:04x}:{args.pid:04x}")
    for index, (interface, info) in enumerate(ports):
        # info.interface is the iInterface string the device published. Two
        # unnamed ACM functions look identical here, which is the whole reason
        # the library fills in a name.
        print(f"  port {index}: {info.device} interface={interface} "
              f"name={info.interface or '(unnamed)'} serial={info.serial_number}")

    if args.expect is not None and len(ports) != args.expect:
        print(f"FAIL expected {args.expect} ports, found {len(ports)}", file=sys.stderr)
        return 1

    failures = 0
    replies = []
    for index, (_, info) in enumerate(ports):
        text = f"probe{index}"
        reply = probe(info.device, text, args.timeout)
        replies.append(reply)
        if not reply:
            print(f"FAIL port {index} ({info.device}): no reply to {text!r}")
            failures += 1
            continue
        # SerialMulti echoes "<PORTNAME>: <char>" per character, so the reply
        # carries the name of the port that received it.
        print(f"  port {index} ({info.device}) -> {reply!r}")

    # Each port must have answered differently: identical replies would mean the
    # nodes are aliases of one function rather than separate pipes.
    distinct = {reply for reply in replies if reply}
    if len(distinct) != len([r for r in replies if r]):
        print("FAIL two ports returned the same reply; they are not independent")
        failures += 1

    print("OK" if failures == 0 else f"NG {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
