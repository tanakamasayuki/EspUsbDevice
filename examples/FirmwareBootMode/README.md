# EspUsbDevice FirmwareBootMode

> 日本語版: [README.ja.md](README.ja.md)

Enters the chip's **ROM download loader** — "boot mode", the state holding BOOT
at reset produces — from the running sketch, so `esptool` can rewrite the whole
flash without anyone touching the board.

This is the recovery path behind every other update route. It works on firmware
with no OTA partition, and on firmware too broken to update itself, because the
loader is in mask ROM and nothing can corrupt it.

## Hardware

- An ESP32-S2, ESP32-S3 or ESP32-P4 board with USB device support
- A PC with `esptool`
- A separate Serial connection for logs

## What It Does

Presents a CDC serial port and a DFU runtime interface, and offers three ways to
ask for the loader — all reaching the same place:

| Gesture | Host side |
|---|---|
| Open the CDC port at 1200 baud and drop DTR | `arduino-cli upload …`, the Arduino IDE, or `stty -F /dev/ttyACM0 1200` |
| `DFU_DETACH` | `dfu-util -e` |
| Send `b` on the CDC port | a script of your own |

Then flash as usual:

```sh
esptool --port <port> write_flash 0x0 firmware.bin
```

## Which port the loader answers on

This differs per chip and is the part that surprises people.

| Chip | After the restart |
|---|---|
| ESP32-S3 | the shared PHY returns to USB Serial/JTAG, so on a one-connector board **the same cable keeps working** |
| ESP32-S2 | the ROM's own CDC on the OTG port |
| ESP32-P4 | the USB Serial/JTAG port — **not** the high-speed OTG connector the device was on |

[Firmware update over USB, section 2.3](../../docs/ota-over-usb.md#23-which-usb-interface-the-rom-answers-on)
has the detail, including what Secure Boot and the `USB_PHY_SEL` eFuse change.

## Key APIs

- `device.rebootToBootloader()` detaches the USB device, sets the target's
  download-boot flag and restarts. Does not return. The flag lives in a
  different register on each chip, and on ESP32-P4 it shares that register with
  the software-reset bit — which is why this is a library call rather than four
  lines in every sketch.
- `device.rebootToRomDfu()` asks the S2/S3 ROM to come up as a DFU device on
  USB-OTG instead, for a host that drives `dfu-util`. Returns `false` without
  restarting on ESP32-P4, and on an ESP32-S3 whose `USB_PHY_SEL` eFuse is not
  burned - measured, the ROM puts its DFU stack on a controller with no pads and
  the connector goes dark, so refusing is the safe answer. For `dfu-util`
  against an unmodified board use [`FirmwareDFU`](../FirmwareDFU/).
- `port.onLineCoding(cb)` delivers the host's requested baud rate, which is how
  the 1200-baud touch is detected.
- `EspUsbDeviceDfu dfu(device, EspUsbDeviceDfuMode::Runtime)` adds the interface
  `dfu-util -e` talks to. Without an `onDetach()` callback it restarts into the
  loader by itself.

## Notes

- **Act on the request from `loop()`, not from the callback.** Every class
  callback in this library runs on the usbd task, and restarting from inside one
  cuts off the control transfer the host is still finishing. This sketch sets a
  flag and restarts from `loop()`.
- **Gate it behind something deliberate.** A stray byte should not drop a user's
  session into a bootloader. The 1200-baud touch is the convention hosts already
  speak.
- Arduino-ESP32's `usb_persist_restart()` does the same job and **cannot be
  linked** from a sketch that uses this library — it pulls in
  `esp32-hal-tinyusb.c`, which defines two of the same TinyUSB callbacks. It is
  also a no-op on ESP32-P4. Use `rebootToBootloader()`.
- The download-boot flag does not persist: after `esptool` flashes and resets,
  the board runs the new application normally.
- This replaces the **whole flash**, unlike [`FirmwareDFU`](../FirmwareDFU/) and
  [`FirmwareHTTP`](../FirmwareHTTP/), which write one OTA partition while the
  sketch keeps running.

## Walkthrough and checks

### Before running

Request the chip ROM loader from CDC or DFU runtime. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

CDC input b, the selected line-coding trigger and DFU detach request set a flag; `loop()` performs `rebootToBootloader()`. This changes the boot mode rather than writing an application image. Expect the USB identity/port to change and use the chip's upload path afterwards.

### API use and adaptation

Use the intended application image for the selected target and partition layout, not a merged Flash dump or bootloader binary. Follow availability/capacity checks before beginning the upload. Transport acknowledgment, received byte count, image validation, boot selection and a successful next boot are different stages. The ROM-loader example changes boot mode rather than using the same in-application update path.

Keep the documented recovery/upload route available before testing. Ordinary firmware replacement can leave NVS data intact, so do not assume a fresh application has fresh settings. `markValid()` concerns application validation, not authenticity of arbitrary received images; deployed products need an explicit authorization and image-verification policy. Reboot/Flash work belongs in the execution path used by the example, not in newly added long-running USB callbacks.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](FirmwareBootMode.ino)
- [All examples](../README.md)
- [Example workflow](../../docs/example-workflow.md)
