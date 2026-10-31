# EspUsbDevice KeyboardMouse

> 日本語版: [README.ja.md](README.ja.md)

Creates a composite USB HID device that exposes keyboard and mouse reports from
one ESP32.

## Hardware

- ESP32-S3, ESP32-S2 or ESP32-P4 board; the native USB OTG port goes to the Host
- A USB Host such as a PC, another ESP32 running EspUsbHost, or a test fixture
- A separate debug connection for `Serial` (115200), used for both input and logs

## What It Does

- Starts `EspUsbDevice` with both `EspUsbDeviceHidKeyboard` and `EspUsbDeviceHidMouse`
- Prints the report IDs `begin()` assigned and the largest Input report
- On `k` from the debug port: types `K`, moves the pointer 40 counts right and
  sends a left click

Focus a scratch editor on the Host before sending `k`.

## Composite HID Shape

Several HID Functions on one device share one HID interface and one merged
report descriptor, and are told apart by report ID.

| Report | Report ID |
|--------|-----------|
| Keyboard | `1` (`ESP_USB_DEVICE_HID_REPORT_ID_KEYBOARD`) |
| Mouse | `2` (`ESP_USB_DEVICE_HID_REPORT_ID_MOUSE`) |

These are the preferred IDs; `keyboard.compositeReportId()` and
`mouse.compositeReportId()` return what was actually assigned (repeated HID types
get unique IDs). Each Function's descriptor is wrapped in Push/Pop in the merged
descriptor, so units and ranges do not leak from one Function to the next.
The send functions add the report ID for you.

## Key APIs

- Construct every Function with the same `EspUsbDevice` before `begin()`:
  `EspUsbDeviceHidKeyboard keyboard(device);`
  `EspUsbDeviceHidMouse mouse(device);`
- Call `device.begin(config)` once, after all Functions are constructed.
- `compositeReportId()` returns the assigned ID (0 when standalone or stopped).
- `inputReportCapacity()` returns the largest Input report of the shared
  interface, including the report ID byte (0 before `begin()`).
- Use the keyboard APIs from [Keyboard](../Keyboard/) and the mouse APIs from
  [Mouse](../Mouse/).

## Expected Serial Output

```text
USB keyboard + mouse ready (keyboard id=1 mouse id=2 input=9 bytes)
Focus a scratch editor on the Host, then send k on this port
```

After that, only failures are printed, for example `KEY_FAILED ESP_ERR_TIMEOUT`.
Watch the keyboard and mouse on the Host.

## Checks

- Compare the printed report IDs with the descriptor a HID parser shows.
- Adding a second Function changes the report layout (a report ID byte is
  added). Do not reuse assumptions from a single-Function raw HID client.
- A HID report is state: key positions, relative mouse deltas, button bitmaps.
  Match each press with a release.

## Changes from v2

- Keys and mouse actions are sent when you send `k`, not every 3 seconds.
- The assigned report IDs are read with the new `compositeReportId()` instead of
  being assumed, and `inputReportCapacity()` (new) shows the largest Input report.
- v2 fixed the composite interrupt packet at 16 bytes inside Arduino-ESP32's
  TinyUSB HID; v3 uses the library's own HID driver and sizes storage from the
  merged descriptor at `begin()`. The shared interrupt IN packet is the largest
  one any member asks for, at least 16 bytes (16 here; an NKRO keyboard or a
  larger Custom/Vendor HID raises it). `keyboard.inputPacketSize()` reads it once
  the Host has reset the bus. The sketch code for keyboard and mouse is unchanged.

## See Also

- [Keyboard](../Keyboard/) - HID keyboard
- [Mouse](../Mouse/) - HID mouse
- [HID API guide](../../docs/classes/hid.md)
- [All examples](../README.md)
