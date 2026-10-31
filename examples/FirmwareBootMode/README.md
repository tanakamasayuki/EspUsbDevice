# EspUsbDevice FirmwareBootMode

> 日本語版: [README.ja.md](README.ja.md)

Enters the chip's **ROM download loader** - "boot mode", the state holding BOOT
at reset produces - from the running sketch, so `esptool` can rewrite the whole
flash without anyone touching the board.

This is the recovery path behind every other update route. It works on firmware
with no OTA partition, and on firmware too broken to update itself, because the
loader is in mask ROM and nothing can corrupt it.

## Hardware

- An ESP32-S2, ESP32-S3 or ESP32-P4 board, connected through its **native USB OTG** port
- A PC with `esptool` (and optionally `dfu-util`)
- A separate Serial connection for logs

## What It Does

Presents a CDC serial port ("Console") and a DFU runtime interface
("Bootloader"), and offers three ways to ask for the loader - all reaching the
same place:

| Gesture | Host side |
|---|---|
| Open the CDC port at 1200 baud and drop DTR | `arduino-cli upload ...`, the Arduino IDE, or `stty -F /dev/ttyACM0 1200` |
| `DFU_DETACH` | `dfu-util -e` (`dfu-util -d 303a:4000 -e` when several devices are attached) |
| Send `b` on the CDC port | `printf b > /dev/ttyACM0`, or a script of your own |

Then flash as usual:

```sh
esptool --port <port> write_flash 0x0 firmware.merged.bin
# or the individual images at their offsets, as arduino-cli upload does
```

A fourth command, `d` on the CDC port, asks for the S2/S3 **ROM DFU** device
instead (see `rebootToRomDfu()` below). On ESP32-P4, and on an ESP32-S3 whose
`USB_PHY_SEL` eFuse is not burned, the library refuses; the sketch then prints
`ROM_DFU_UNSUPPORTED` and replies on the CDC port, and keeps running.

## Which port the loader answers on

This differs per chip and is the part that surprises people.

| Chip | After the restart |
|---|---|
| ESP32-S3 | the shared PHY returns to USB Serial/JTAG, so on a one-connector board **the same cable keeps working** (a new `/dev/ttyACM*` / COM port appears) |
| ESP32-S2 | the ROM's own CDC on the OTG port |
| ESP32-P4 | the USB Serial/JTAG port - **not** the high-speed OTG connector the device was on |

[Firmware update over USB, section 2.3](../../docs/ota-over-usb.md#23-which-usb-port-the-rom-answers-on)
has the detail, including what Secure Boot and the `USB_PHY_SEL` eFuse change.

## Key APIs

- `device.rebootToBootloader()` detaches the USB device, sets the target's
  download-boot flag and restarts. Does not return. The flag lives in a
  different register on each chip, and on ESP32-P4 it shares that register with
  the software-reset bit - which is why this is a library call rather than four
  lines in every sketch.
- `device.rebootToRomDfu()` asks the S2/S3 ROM to come up as a DFU device on
  USB-OTG instead, for a host that drives `dfu-util` (with the `dfu.bin` that
  `idf.py dfu` produces). Returns `false` without restarting on ESP32-P4, and on
  an ESP32-S3 whose `USB_PHY_SEL` eFuse is not burned - measured, the ROM puts
  its DFU stack on a controller with no pads and the connector goes dark, so
  refusing is the safe answer. `device.lastErrorName()` then says
  `ESP_ERR_NOT_SUPPORTED`. For `dfu-util` against an unmodified board use
  [FirmwareDFU](../FirmwareDFU/).
- `port.onLineCoding(cb)` delivers the host's requested baud rate, which is how
  the 1200-baud touch is detected.
- `EspUsbDeviceDfu dfu(device, EspUsbDeviceDfuMode::Runtime, "Bootloader")`
  adds the interface `dfu-util -e` talks to. With no handler it restarts into
  the ROM loader by itself about 100 ms after the DETACH request completes.
- `dfu.setDetachHandler(cb)` replaces that restart: the handler runs once,
  after the request's status stage, and then owns leaving the bus. This sketch
  sets a flag and restarts from `loop()`.
- `dfu.onDetach(cb)` (not used here) decides at SETUP whether to accept the
  DETACH; returning `false` stalls it and changes nothing.

## Expected Serial Output

```text
Ready. Touch the CDC port at 1200 baud, run dfu-util -e, or send 'b' ('d' asks for the S2/S3 ROM DFU instead).
Restarting into the ROM download loader
```

## Notes

- **Act on the request from `loop()`, not from the callback.** Class callbacks
  run on the USB worker, and restarting from inside one cuts off the control
  transfer the host is still finishing. This sketch sets a flag and restarts
  from `loop()`; CDC input is also read in `loop()`.
- **Gate it behind something deliberate.** A stray byte should not drop a
  user's session into a bootloader. The 1200-baud touch is the convention hosts
  already speak; the single-letter commands here are for a bench.
- Arduino-ESP32's `usb_persist_restart()` does the same job and **cannot be
  linked** from a sketch that uses this library - it pulls in
  `esp32-hal-tinyusb.c`, which defines two of the same TinyUSB callbacks. It is
  also a no-op on ESP32-P4. Use `rebootToBootloader()`
  ([guide, 2.5](../../docs/ota-over-usb.md#25-do-not-use-usb_persist_restart)).
- The download-boot flag does not persist: after `esptool` flashes and resets,
  the board runs the new application normally.
- This replaces the **whole flash**, unlike [FirmwareDFU](../FirmwareDFU/) and
  [FirmwareHTTP](../FirmwareHTTP/), which write one OTA partition while the
  sketch keeps running. No OTA partition is needed here.
- Secure Boot / disabled download mode eFuses turn this route off
  ([guide, 2.7](../../docs/ota-over-usb.md#27-what-disables-all-of-this)).

## Checks

Expect the USB identity and port to change after the restart, then use the
chip's upload path. This example changes the boot mode; it does not write an
application image itself.

## Changes from v2

- DFU Runtime: v2's `dfu.onDetach(void callback)`, which replaced the default
  restart, is now `dfu.setDetachHandler()`. `onDetach()` became a
  `bool` accept/refuse decision taken at SETUP.
- The `b` command is read in `loop()` instead of inside `port.onRx()`.
- New `d` command demonstrating `device.rebootToRomDfu()` and its refusal path
  (documented but not exercised in v2).
- The CDC rings are sized with `port.configure()`; the DFU log line moved from
  the callback to `loop()`.

## See Also

- [FirmwareDFU](../FirmwareDFU/) - DFU Download mode (application update)
- [FirmwareMSC](../FirmwareMSC/), [FirmwareHTTP](../FirmwareHTTP/),
  [FirmwareCDC](../FirmwareCDC/), [FirmwareVendor](../FirmwareVendor/) - other
  update routes
- [Firmware update over USB](../../docs/ota-over-usb.md)
- [DFU API guide](../../docs/classes/dfu.md),
  [CDC API guide](../../docs/classes/cdc.md)
- [Sketch](FirmwareBootMode.ino) / [All examples](../README.md) /
  [Example workflow](../../docs/example-workflow.md)
