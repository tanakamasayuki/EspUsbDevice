# s3_single_cable

> 日本語版: [README.ja.md](README.ja.md)

**Does the connector the sketch was using come back as the ROM's loader?**

This is the claim behind
[docs/ota-over-usb.md, 2.3](../../../docs/ota-over-usb.md#23-which-usb-interface-the-rom-answers-on):
that on a one-connector ESP32-S3 the same cable that carried your USB device
carries the ROM's serial loader after `rebootToBootloader()`. It needs a board
whose **native USB is wired to the host** and a person to watch the host, which
is why it lives here rather than in `tests/single`.

It also checks that `rebootToRomDfu()` **refuses** rather than restarting into a
connector nobody can reach.

## What it needs

- An ESP32-S3 whose native USB (GPIO19/20) is connected to the PC
- A **separate** UART for flashing and logs. The native USB is the thing under
  test; you cannot use it to recover
- A way to see what the host enumerates (`usbipd list` on Windows,
  `lsusb` / `dmesg` on Linux)

## Why the separate UART is not optional

The whole point of the test is to take the native USB away from the host and
give it back. If the give-back is broken - which is exactly what this test
caught once - the connector goes dark and the only other way in is the BOOT
button.

## Running it

```sh
# Flash over the UART, never over the native USB.
arduino-cli compile --profile esp32s3 .
arduino-cli upload --profile esp32s3 --port /dev/ttyACM3 .
```

The sketch starts a vendor + DFU device, waits 20 seconds, then reboots.
`-DS3_CABLE_ACTION=1` selects `rebootToRomDfu()` instead of
`rebootToBootloader()`.

**Do not open the UART while the 20 seconds are running.** On a board whose
auto-reset is wired to DTR/RTS, opening it resets the chip and you measure
nothing. Watch the host instead.

## What to expect

`rebootToBootloader()`, watching one connector:

| When | Host sees |
|---|---|
| sketch running | the sketch's own VID:PID (`303a:4095` as written) |
| after the reboot | `303a:1001`, USB Serial/JTAG, and it stays there |

Then `esptool --port <that port> --before default-reset chip-id` must upload and
run its stub flasher. On Windows that is
`uv run --with esptool esptool --port COM12 ...`; from WSL, attach the port
first.

`rebootToRomDfu()` on a board whose `USB_PHY_SEL` eFuse is not burned:

```
S3CABLE_ROM_DFU_UNSUPPORTED ESP_ERR_NOT_SUPPORTED
```

and **the device keeps running** - the connector still shows the sketch. A
connector that goes dark here is the regression this test exists for.

## The failure it was written for

Before the fix, `rebootToBootloader()` left the PHY selection pointing at
USB-OTG. That selection lives in `RTC_CNTL_USB_CONF_REG`, which is in the RTC
domain and **survives a software reset**, so the ROM's loader came up with the
pads routed to a controller it was not driving. Measured: the USB port vanished
from the host entirely while `esptool` over the UART still connected with
`--before no-reset` - the chip was in the loader, and nobody could reach it.
