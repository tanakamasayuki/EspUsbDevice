# EspUsbDevice Serial

> 日本語版: [README.ja.md](README.ja.md)

Creates a USB CDC ACM serial device for text communication with a PC or USB
host.

This sketch does not use Arduino-ESP32's `USBSerial` / `USBCDC`. It registers
`EspUsbDeviceCdcSerial` with `EspUsbDevice`, and this library owns the USB CDC
class descriptors and endpoints.

## Hardware

- ESP32-S3 or another Arduino-ESP32 board with USB device support
- A USB host such as a PC, another ESP32 running EspUsbHost, or a test fixture
- A separate Serial monitor connection for logs when available

## What It Does

- Starts `EspUsbDevice` as a full-speed CDC ACM serial device
- Sends a `tick=...` message to USB CDC every 3 seconds
- Echoes each received USB CDC character as `echo: x`
- Prints host line coding and DTR / RTS changes to the regular Serial monitor

## Usage

1. Flash the sketch and open the regular Serial monitor.
2. Connect the USB device port to the PC or host.
3. Open the newly created serial port on the PC.
4. Send characters and observe `echo: ...` replies.
5. The regular Serial monitor prints `CDC_LINE_CODING` and `CDC_LINE_STATE`.

## Key APIs

- `EspUsbDeviceCdcSerial UsbSerial(device)` registers the CDC ACM function.
- `UsbSerial.available()` returns bytes received from the host.
- `UsbSerial.read()` reads bytes from the host.
- `UsbSerial.write()` / `print()` / `printf()` send data to the host.
- `UsbSerial.flush()` flushes outgoing data.
- `UsbSerial.connected()` treats DTR as the connected state.
- `UsbSerial.onLineCoding(callback)` receives baud / stop bits / parity / data
  bits changes.
- `UsbSerial.onLineState(callback)` receives DTR / RTS changes.
- `UsbSerial.onRx(callback)` runs when host data is received.

## Notes

- USB CDC and the logging Serial monitor are separate paths.
- The PC serial port name depends on the OS.
- Baud rate is reported as USB CDC line coding; it is not the actual USB
  transfer speed.
- This library is not designed to run together with Arduino's built-in USB
  device classes.

## See Also

- [Keyboard](../Keyboard/) - HID keyboard device
- [Mouse](../Mouse/) - HID mouse device

## Walkthrough and checks

### Before running

Create a CDC serial function with receive and line-state diagnostics. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

Declare UsbSerial on the device, register callbacks, then begin the device. Open the new USB serial port and send letters plus CR/LF: this sketch replies per character, not once per input line. Compare DTR and periodic tick output with a terminal open and closed.

### API use and adaptation

`device.ready()` concerns USB configuration; the CDC `connected()` state concerns host DTR. They answer different questions. The callback's receive amount is not a line or packet boundary: read the actual bytes and accumulate your own complete messages. Use `write()` for bytes and `print()`/`printf()` for text formatting. `flush()` is not a guarantee that the PC application consumed the data.

Line-coding callbacks report the host's requested serial parameters; they do not change USB speed. Keep callbacks short and use a synchronized handoff to `loop()` for larger parsing. For reliable commands, check the number of bytes accepted, keep any unsent suffix, and decide what partial input means after disconnect.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](Serial.ino)
- [All examples](../README.md)
- [Example workflow](../../docs/example-workflow.md)
