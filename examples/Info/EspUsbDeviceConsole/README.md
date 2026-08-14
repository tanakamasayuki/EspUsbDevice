# EspUsbDeviceConsole

> 日本語版: [README.ja.md](README.ja.md)

Drive the USB device **by hand from a serial terminal**. Send one HID report or
vendor transfer at a time and watch how the host reacts, without rebuilding the
sketch for every experiment.

It also prints everything coming *down* from the host - HID output reports, HID
protocol switches, vendor control requests, vendor bulk OUT - with a `HOST_`
prefix.

## Hardware

- An Arduino-ESP32 board with USB device support (ESP32-S2 / S3 / P4)
- The USB host under test (a PC, typically)
- **A serial connection for typing commands** (the UART / USB Serial-JTAG side
  used for flashing)

Two serial connections are involved: commands go to the flashing port, and the
USB device connector goes to the host under test. On a board with a single
connector, use a peer board or accept that the log disappears when you unplug.

## Commands

| Command | Meaning |
|---|---|
| `help` | List the commands |
| `state` | Mount state, speed, LEDs, HID protocol |
| `text <string>` | Send the string as keystrokes |
| `key <usage> [modifiers]` | Tap one raw HID usage, e.g. `key 0x04 0x02` |
| `hold <usage> [modifiers]` | Press and keep held |
| `release` | Release everything held |
| `mouse <dx> <dy> [buttons] [wheel]` | Send a mouse report |
| `click <1\|2\|4>` | Left / right / middle click |
| `report <id> <hex...>` | Send a raw HID report, e.g. `report 1 00 00 04 00 00 00 00 00` |
| `vendor <hex...>` | Raw bytes on the vendor bulk IN endpoint |
| `vendortext <string>` | The same, as ASCII |

Numbers are accepted as `0x04` or `4`. Captures and datasheets mix hex and
decimal, and converting them by hand while transcribing is where the errors come
from.

## What It Is For

- **Finding out which usage a host reacts to.** When an application only picks
  up certain keys, or a game only reads one button layout, try them one at a
  time.
- **Trying a raw report before committing to a report descriptor.** `report`
  puts an arbitrary byte string on the wire so you can see how the host reads it
  before writing the descriptor.
- **Watching what the host sends.** Run the host's own software while watching
  `HOST_VENDOR_CONTROL` / `HOST_VENDOR_OUT` / `HOST_HID_OUTPUT` to learn the
  initialization sequence it expects.
- **Seeing the boot-protocol switch.** BIOS / UEFI ask for boot protocol;
  `HOST_HID_PROTOCOL` shows the moment it happens.

Vendor control requests are answered with a fixed banner by default. Edit
`onControlRequest` in the sketch to replay a real protocol instead.

## Notes

- Sends made while not mounted are dropped, not queued. Check `state` first.
- The `vendor` commands need the host to have claimed the interface (libusb /
  WinUSB / WebUSB). Until then `vendor_mounted` stays 0.
- The device registers as a keyboard and mouse, so **keystrokes really land in
  whatever has focus on the host.** Test with a scratch text editor in front.

## Expected Serial Output

```text
=== EspUsbDevice console ===
BEGIN ok - connect the device connector to the host under test
MOUNTED
HOST_HID_PROTOCOL instance=0 protocol=report
state
STATE mounted=1 speed=full leds=0x00 hid_protocol=report vendor_mounted=0
key 0x04
SEND key usage=0x04 modifiers=0x00 ok=1
report 1 00 00 05 00 00 00 00 00
SEND report id=1 length=8 ok=1 data=00 00 05 00 00 00 00 00
HOST_HID_OUTPUT leds=0x02 num=0 caps=1 scroll=0
```

## Related

- [EspUsbDeviceBringUpCheck](../EspUsbDeviceBringUpCheck/) - start here when nothing enumerates
- [EspUsbDeviceDescriptorDump](../EspUsbDeviceDescriptorDump/) - check what you are declaring
- [docs/usb-device-guide.md](../../../docs/usb-device-guide.md) - the full bring-up procedure
