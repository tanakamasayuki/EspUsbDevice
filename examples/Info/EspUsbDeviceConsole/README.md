# EspUsbDeviceConsole

> 日本語版: [README.ja.md](README.ja.md)

Drive the USB device **by hand from a serial terminal**. Send one HID report or
vendor transfer at a time and watch how the host reacts, without rebuilding the
sketch for every experiment.

It also prints everything coming *down* from the host - HID output reports, HID
protocol switches, vendor control requests (with their data), vendor bulk OUT -
with a `HOST_` prefix.

## Hardware

- An Arduino-ESP32 board with USB device support (ESP32-S2 / S3 / P4)
- The USB host under test (a PC, typically)
- **A serial connection for typing commands** (the UART / USB Serial-JTAG side
  used for flashing)

Two connections are involved: commands go to the flashing port, and the USB
device connector goes to the host under test. On a board with a single
connector, use a peer board or accept that the log disappears when you unplug.

## Commands

| Command | Meaning |
|---|---|
| `help` | List the commands |
| `state` | Mount and suspend state, LEDs, HID protocol, vendor state and free send space |
| `text <string>` | Send the string as keystrokes |
| `key <usage> [modifiers]` | Tap one raw HID usage, e.g. `key 0x04 0x02` |
| `hold <usage> [modifiers]` | Press and keep held |
| `release` | Release everything held |
| `mouse <dx> <dy> [buttons] [wheel]` | Send a mouse report |
| `click <1\|2\|4>` | Left / right / middle click |
| `report <id> <hex...>` | Send a raw HID input report, e.g. `report 1 00 00 04 00 00 00 00 00` |
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
  puts an arbitrary byte string on the wire so you can see how the host reads it.
  Keyboard and mouse share HID interface 0 and are told apart by report ID
  (keyboard 1, mouse 2). The payload excludes the report ID byte.
- **Watching what the host sends.** Run the host's own software while watching
  `HOST_VENDOR_CONTROL` / `HOST_VENDOR_CONTROL_DATA` / `HOST_VENDOR_OUT` /
  `HOST_HID_OUTPUT` to learn the initialization sequence it expects.
- **Seeing the boot-protocol switch.** BIOS / UEFI ask for boot protocol;
  `HOST_HID_PROTOCOL` shows the moment it happens.

Vendor control requests are answered with a fixed banner (IN) or by receiving up
to 64 bytes (OUT) by default. Edit `handleVendorControl()` in the sketch to
replay a real protocol instead. `bRequest = 0x02` with `wIndex = 7` is the
Microsoft OS 2.0 request, which the library answers itself before the sketch
sees it.

## Notes

- Sends made while not mounted are refused, not queued. Check `state` first.
- `vendor` needs the host to have configured the device; a host application
  must still read bulk IN (libusb / WinUSB / WebUSB) for the bytes to leave the
  send ring. `written` is what the ring accepted.
- Bulk OUT is received in callback mode: each `HOST_VENDOR_OUT` line is one
  received USB packet, not necessarily one host write.
- The device registers as a keyboard and mouse, so **keystrokes really land in
  whatever has focus on the host.** Test with a scratch text editor in front.
- All callbacks run on the USB worker and are registered before `begin()`.

## Expected Serial Output

```text
=== EspUsbDevice console ===
BEGIN ok - connect the device connector to the host under test
MOUNTED
HOST_HID_PROTOCOL instance=0 protocol=report
state
STATE mounted=1 suspended=0 leds=0x00 hid_protocol=report vendor_mounted=1 vendor_tx_free=512
key 0x04
SEND key usage=0x04 modifiers=0x00 ok=1
report 1 00 00 05 00 00 00 00 00
SEND report id=1 length=8 ok=1 error=ESP_OK data=00 00 05 00 00 00 00 00
HOST_HID_OUTPUT leds=0x02 num=0 caps=1 scroll=0
```

## Related

- [EspUsbDeviceBringUpCheck](../EspUsbDeviceBringUpCheck/) - start here when nothing enumerates
- [EspUsbDeviceDescriptorDump](../EspUsbDeviceDescriptorDump/) - check what you are declaring
- [docs/usb-device-guide.md](../../../docs/usb-device-guide.md) - the full bring-up procedure
- [HID API (v3)](../../../docs/classes/hid.md), [Vendor API (v3)](../../../docs/classes/vendor.md)

## Walkthrough and checks

### Before running

Control HID and vendor data explicitly from a diagnostic console. Follow the hardware requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

Begin with help/state, then use text or a single key on a scratch document. Distinguish key/tap from hold/release and verify release before another test. The UART command interface controls generated USB actions; it is not the Vendor bulk interface itself.

### API use and adaptation

Treat startup, enumeration, interface discovery and actual class communication as separate checkpoints. A descriptor says what a device advertises; it does not prove a transfer completed or that a protocol is implemented. An interface number, endpoint address, USB address and physical port identify different things.

First compare the same device directly connected and reconnected. Add a hub or more Functions only after the simple case is understood. Preserve the error name and the operation that produced it. Logging from callbacks is diagnostic work, not a pattern for high-rate traffic.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the host absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one command or reply. The demonstration's error logging is not a general retry or recovery policy.

## Changes from v2

- **`state` no longer shows the bus speed.** v2 read it with TinyUSB's
  `tud_speed_get()`; v3 has no public query for the negotiated speed. `state`
  shows `suspended` and the vendor send space instead.
- Vendor control requests are handled per stage. An OUT request with data now
  gets a receive buffer, and its bytes are printed as `HOST_VENDOR_CONTROL_DATA`
  after the ACK; v2 acknowledged only the status stage. Requests longer than
  64 bytes are stalled.
- Bulk OUT uses `rxMode = Callback` with `onRxData()`, so no receive ring is
  allocated; v2 read a FIFO inside `onRx()`.
- `report` prints `lastErrorName()` when the send fails.
- `vendor` no longer calls `flush()`; the default `autoFlush` starts the send.
- Callback registration and `configure()` results are checked before `begin()`.

- [Sketch](EspUsbDeviceConsole.ino)
- [All examples](../../README.md)
- [Example workflow](../../../docs/example-workflow.md)
