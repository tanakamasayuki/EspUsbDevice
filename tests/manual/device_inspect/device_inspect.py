#!/usr/bin/env python3
"""Dump an EspUsbDevice board's descriptors as the host received them.

The device-side counterpart of ``examples/Info/EspUsbDeviceDescriptorDump``:
that sketch prints what the library built, this prints what actually arrived on
the host. They must agree byte for byte. When they do not, the bytes never left
the device, and the problem is below the descriptor layer.

Run from ``tests``:

    uv run --with pyusb python manual/device_inspect/device_inspect.py
    uv run --with pyusb python manual/device_inspect/device_inspect.py --pid 0x4051
    uv run --with pyusb python manual/device_inspect/device_inspect.py --json > seen.json
"""

from __future__ import annotations

import argparse
import json
import sys

try:
    import usb.core
    import usb.util
except ImportError:
    sys.exit("PyUSB is required: run this script with `uv run --with pyusb`")


DEFAULT_VID = 0x303A

DT_DEVICE = 0x01
DT_CONFIGURATION = 0x02
DT_STRING = 0x03
DT_INTERFACE = 0x04
DT_ENDPOINT = 0x05
DT_DEVICE_QUALIFIER = 0x06
DT_OTHER_SPEED_CONFIGURATION = 0x07
DT_INTERFACE_ASSOCIATION = 0x0B
DT_BOS = 0x0F
DT_HID = 0x21
DT_HID_REPORT = 0x22
DT_CS_INTERFACE = 0x24
DT_CS_ENDPOINT = 0x25

SPEED_NAMES = {1: "low (1.5 Mbps)", 2: "full (12 Mbps)", 3: "high (480 Mbps)"}

CLASS_NAMES = {
    0x00: "per-interface",
    0x01: "Audio",
    0x02: "CDC control",
    0x03: "HID",
    0x05: "Physical",
    0x07: "Printer",
    0x08: "Mass Storage",
    0x09: "Hub",
    0x0A: "CDC data",
    0x0B: "Smart Card (CCID)",
    0x0E: "Video",
    0xDC: "Diagnostic",
    0xEF: "Miscellaneous",
    0xFE: "Application specific",
    0xFF: "Vendor specific",
}

TRANSFER_NAMES = {0: "control", 1: "isochronous", 2: "bulk", 3: "interrupt"}


def class_name(code: int) -> str:
    return CLASS_NAMES.get(code, "unknown")


def usb_device_node(device) -> str | None:
    bus = getattr(device, "bus", None)
    address = getattr(device, "address", None)
    if bus is None or address is None:
        return None
    return f"/dev/bus/usb/{bus:03d}/{address:03d}"


def permission_hint(device, error) -> str:
    if getattr(error, "errno", None) != 13:
        return ""
    node = usb_device_node(device)
    fix = (
        f"`sudo chmod a+rw {node}`"
        if node
        else "grant the current user read/write access to the USB device"
    )
    return (
        f"\n  (access denied - temporary fix: {fix}; permanent: install a udev "
        "rule, see tests/manual/README.md)"
    )


def get_descriptor(device, descriptor_type: int, index: int, length: int,
                   wIndex: int = 0, request_type: int = 0x80) -> bytes:
    return bytes(
        device.ctrl_transfer(
            request_type,
            0x06,  # GET_DESCRIPTOR
            (descriptor_type << 8) | index,
            wIndex,
            length,
            timeout=2000,
        )
    )


def try_get_descriptor(device, descriptor_type: int, index: int, length: int,
                       wIndex: int = 0, request_type: int = 0x80) -> bytes | None:
    try:
        return get_descriptor(device, descriptor_type, index, length, wIndex,
                              request_type)
    except usb.core.USBError:
        return None


def hex_lines(data: bytes) -> list[str]:
    lines = []
    for offset in range(0, len(data), 16):
        chunk = data[offset:offset + 16]
        columns = " ".join(f"{byte:02x}" for byte in chunk)
        lines.append(f"  {offset:04x}  {columns}")
    return lines


def print_hex(data: bytes) -> None:
    for line in hex_lines(data):
        print(line)


def walk_configuration(data: bytes) -> list[str]:
    """Decode a configuration descriptor the way a host parser reads it."""
    lines: list[str] = []
    offset = 0
    while offset + 1 < len(data):
        length = data[offset]
        block_type = data[offset + 1]
        if length < 2 or offset + length > len(data):
            lines.append(
                f"  {offset:04x}  MALFORMED bLength={length} "
                f"remaining={len(data) - offset}"
            )
            break
        block = data[offset:offset + length]
        prefix = f"  {offset:04x}  "
        if block_type == DT_CONFIGURATION and length >= 9:
            lines.append(
                prefix
                + f"CONFIGURATION total={block[2] | (block[3] << 8)} "
                f"interfaces={block[4]} value={block[5]} "
                f"attributes=0x{block[7]:02x} power={block[8] * 2}mA"
            )
        elif block_type == DT_INTERFACE and length >= 9:
            lines.append(
                prefix
                + f"INTERFACE  number={block[2]} alt={block[3]} "
                f"endpoints={block[4]} class=0x{block[5]:02x} "
                f"({class_name(block[5])}) subclass=0x{block[6]:02x} "
                f"protocol=0x{block[7]:02x}"
            )
        elif block_type == DT_ENDPOINT and length >= 7:
            address = block[2]
            direction = "IN" if address & 0x80 else "OUT"
            transfer = TRANSFER_NAMES[block[3] & 0x03]
            lines.append(
                prefix
                + f"ENDPOINT   0x{address:02x} {direction:<3} {transfer:<11} "
                f"mps={block[4] | (block[5] << 8)} interval={block[6]}"
            )
        elif block_type == DT_INTERFACE_ASSOCIATION and length >= 8:
            lines.append(
                prefix
                + f"IAD        first={block[2]} count={block[3]} "
                f"class=0x{block[4]:02x} ({class_name(block[4])}) "
                f"subclass=0x{block[5]:02x} protocol=0x{block[6]:02x}"
            )
        elif block_type == DT_HID and length >= 9:
            lines.append(
                prefix
                + f"HID        bcdHID=0x{block[3] << 8 | block[2]:04x} "
                f"descriptors={block[5]} "
                f"report_descriptor={block[7] | (block[8] << 8)} bytes"
            )
        elif block_type == DT_CS_INTERFACE and length >= 3:
            lines.append(prefix + f"CS_INTERFACE subtype=0x{block[2]:02x} length={length}")
        elif block_type == DT_CS_ENDPOINT and length >= 3:
            lines.append(prefix + f"CS_ENDPOINT  subtype=0x{block[2]:02x} length={length}")
        else:
            lines.append(prefix + f"TYPE 0x{block_type:02x}  length={length}")
        offset += length
    return lines


def hid_interfaces(config: bytes) -> list[tuple[int, int]]:
    """(interface number, report descriptor length) for every HID interface."""
    found: list[tuple[int, int]] = []
    offset = 0
    current_interface: int | None = None
    while offset + 1 < len(config):
        length = config[offset]
        block_type = config[offset + 1]
        if length < 2 or offset + length > len(config):
            break
        if block_type == DT_INTERFACE and length >= 9:
            current_interface = config[offset + 2]
        elif block_type == DT_HID and length >= 9 and current_interface is not None:
            report_length = config[offset + 7] | (config[offset + 8] << 8)
            found.append((current_interface, report_length))
        offset += length
    return found


def read_string(device, index: int) -> str | None:
    if index == 0:
        return None
    try:
        return usb.util.get_string(device, index)
    except (usb.core.USBError, ValueError):
        return None


def read_configuration(device, index: int) -> bytes | None:
    header = try_get_descriptor(device, DT_CONFIGURATION, index, 9)
    if not header or len(header) < 4:
        return None
    total = header[2] | (header[3] << 8)
    return try_get_descriptor(device, DT_CONFIGURATION, index, total)


def read_hid_report_descriptor(device, interface: int, length: int) -> tuple[bytes | None, str]:
    """Read a HID report descriptor, detaching the kernel driver if needed.

    Returns (data, note). The note explains a failure rather than hiding it: on
    Linux usbhid owns HID interfaces, and on Windows the HID driver does not
    allow this request at all.
    """
    detached = False
    try:
        if hasattr(device, "is_kernel_driver_active") and device.is_kernel_driver_active(interface):
            device.detach_kernel_driver(interface)
            detached = True
    except (usb.core.USBError, NotImplementedError):
        pass
    try:
        # Interface-targeted GET_DESCRIPTOR: bmRequestType 0x81, wIndex = interface.
        data = get_descriptor(device, DT_HID_REPORT, 0, length, interface, 0x81)
        return data, ""
    except usb.core.USBError as error:
        return None, f"could not be read ({error})"
    finally:
        if detached:
            try:
                device.attach_kernel_driver(interface)
            except (usb.core.USBError, NotImplementedError):
                pass


def collect(device, read_hid: bool) -> dict:
    raw_device = try_get_descriptor(device, DT_DEVICE, 0, 18)
    result: dict = {
        "bus": getattr(device, "bus", None),
        "address": getattr(device, "address", None),
        "speed": getattr(device, "speed", None),
        "speed_name": SPEED_NAMES.get(getattr(device, "speed", None), "unknown"),
        "idVendor": device.idVendor,
        "idProduct": device.idProduct,
        "bcdUSB": device.bcdUSB,
        "bDeviceClass": device.bDeviceClass,
        "bDeviceSubClass": device.bDeviceSubClass,
        "bDeviceProtocol": device.bDeviceProtocol,
        "bMaxPacketSize0": device.bMaxPacketSize0,
        "bNumConfigurations": device.bNumConfigurations,
        "manufacturer": read_string(device, device.iManufacturer),
        "product": read_string(device, device.iProduct),
        "serial": read_string(device, device.iSerialNumber),
        "device_descriptor": raw_device.hex() if raw_device else None,
        "configurations": [],
        "device_qualifier": None,
        "other_speed_configuration": None,
        "bos": None,
        "hid_report_descriptors": [],
    }

    for index in range(device.bNumConfigurations):
        config = read_configuration(device, index)
        if config is None:
            continue
        result["configurations"].append(
            {"index": index, "raw": config.hex(), "decoded": walk_configuration(config)}
        )

    qualifier = try_get_descriptor(device, DT_DEVICE_QUALIFIER, 0, 10)
    if qualifier:
        result["device_qualifier"] = qualifier.hex()

    other_header = try_get_descriptor(device, DT_OTHER_SPEED_CONFIGURATION, 0, 9)
    if other_header and len(other_header) >= 4:
        total = other_header[2] | (other_header[3] << 8)
        other = try_get_descriptor(device, DT_OTHER_SPEED_CONFIGURATION, 0, total)
        if other:
            result["other_speed_configuration"] = {
                "raw": other.hex(),
                "decoded": walk_configuration(other),
            }

    bos_header = try_get_descriptor(device, DT_BOS, 0, 5)
    if bos_header and len(bos_header) >= 4:
        total = bos_header[2] | (bos_header[3] << 8)
        bos = try_get_descriptor(device, DT_BOS, 0, total)
        if bos:
            result["bos"] = bos.hex()

    if read_hid and result["configurations"]:
        first_config = bytes.fromhex(result["configurations"][0]["raw"])
        for interface, length in hid_interfaces(first_config):
            data, note = read_hid_report_descriptor(device, interface, length)
            result["hid_report_descriptors"].append(
                {
                    "interface": interface,
                    "length": length,
                    "raw": data.hex() if data else None,
                    "note": note,
                }
            )

    return result


def report(info: dict, show_raw: bool) -> None:
    print("=" * 72)
    print(
        f"DEVICE {info['idVendor']:04x}:{info['idProduct']:04x} "
        f"bus={info['bus']} address={info['address']} speed={info['speed_name']}"
    )
    print(f"  manufacturer {info['manufacturer']!r}")
    print(f"  product      {info['product']!r}")
    print(f"  serial       {info['serial']!r}")
    print(
        f"  bcdUSB=0x{info['bcdUSB']:04x} class=0x{info['bDeviceClass']:02x} "
        f"({class_name(info['bDeviceClass'])}) "
        f"subclass=0x{info['bDeviceSubClass']:02x} "
        f"protocol=0x{info['bDeviceProtocol']:02x} "
        f"ep0_mps={info['bMaxPacketSize0']} "
        f"configurations={info['bNumConfigurations']}"
    )
    if show_raw and info["device_descriptor"]:
        print("--- DEVICE descriptor ---")
        print_hex(bytes.fromhex(info["device_descriptor"]))

    for config in info["configurations"]:
        raw = bytes.fromhex(config["raw"])
        print(f"--- CONFIGURATION descriptor {config['index']} ({len(raw)} bytes) ---")
        if show_raw:
            print_hex(raw)
        for line in config["decoded"]:
            print(line)

    if info["device_qualifier"]:
        print("--- DEVICE QUALIFIER ---")
        print_hex(bytes.fromhex(info["device_qualifier"]))
    else:
        print("--- DEVICE QUALIFIER: not answered (full-speed only device) ---")

    other = info["other_speed_configuration"]
    if other:
        raw = bytes.fromhex(other["raw"])
        print(f"--- OTHER SPEED CONFIGURATION ({len(raw)} bytes) ---")
        if show_raw:
            print_hex(raw)
        for line in other["decoded"]:
            print(line)

    if info["bos"]:
        print("--- BOS descriptor ---")
        print_hex(bytes.fromhex(info["bos"]))
    else:
        print("--- BOS descriptor: none (WebUSB disabled) ---")

    for entry in info["hid_report_descriptors"]:
        if entry["raw"]:
            raw = bytes.fromhex(entry["raw"])
            print(
                f"--- HID report descriptor, interface {entry['interface']} "
                f"({len(raw)} bytes) ---"
            )
            print_hex(raw)
        else:
            print(
                f"--- HID report descriptor, interface {entry['interface']}: "
                f"{entry['note']} ---"
            )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Dump EspUsbDevice descriptors as the host received them"
    )
    parser.add_argument("--vid", type=lambda v: int(v, 0), default=DEFAULT_VID,
                        help="vendor ID (default: 0x303a)")
    parser.add_argument("--pid", type=lambda v: int(v, 0), default=None,
                        help="product ID (default: every device with this VID)")
    parser.add_argument("--json", action="store_true",
                        help="emit JSON instead of a report, for diffing")
    parser.add_argument("--raw", action="store_true", default=True,
                        help="include hex dumps (default)")
    parser.add_argument("--no-raw", dest="raw", action="store_false",
                        help="decoded lines only")
    parser.add_argument("--no-hid", dest="hid", action="store_false", default=True,
                        help="skip HID report descriptors (they need the kernel "
                             "driver detached on Linux)")
    args = parser.parse_args()

    finder = {"idVendor": args.vid}
    if args.pid is not None:
        finder["idProduct"] = args.pid
    devices = list(usb.core.find(find_all=True, **finder))
    if not devices:
        target = f"{args.vid:04x}:{args.pid:04x}" if args.pid is not None else f"{args.vid:04x}:*"
        sys.exit(
            f"no USB device matching {target} found.\n"
            "Flash an EspUsbDevice sketch, connect the device connector to this "
            "PC, and check `lsusb` (Linux) or Device Manager (Windows) first."
        )

    collected = []
    for device in devices:
        try:
            collected.append(collect(device, args.hid))
        except usb.core.USBError as error:
            print(
                f"skipped {device.idVendor:04x}:{device.idProduct:04x}: {error}"
                f"{permission_hint(device, error)}",
                file=sys.stderr,
            )
        finally:
            usb.util.dispose_resources(device)

    if not collected:
        return 1

    if args.json:
        print(json.dumps(collected, indent=2))
    else:
        for info in collected:
            report(info, args.raw)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
