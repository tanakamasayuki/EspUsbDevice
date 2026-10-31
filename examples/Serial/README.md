# EspUsbDevice Serial

> 日本語版: [README.ja.md](README.ja.md)

Creates a USB CDC ACM serial device for text communication with a PC or USB
host.

This sketch does not use Arduino-ESP32's `USBSerial` / `USBCDC`. It registers
`EspUsbDeviceCdcSerial` with `EspUsbDevice`, and this library owns the USB CDC
descriptors and endpoints. The receive/transmit ring sizes are set at run time
with `configure()`; no `build_opt.h` is needed.

## Hardware

- ESP32-S2 / S3 / P4 board with a native USB OTG port
- A USB host such as a PC, another ESP32 running EspUsbHost, or a test fixture
- A separate Serial monitor connection for logs (the UART / USB Serial-JTAG
  side used for flashing)

## What It Does

- Configures the CDC port with a 256-byte receive ring and a 1024-byte transmit
  ring, then starts `EspUsbDevice`
- Echoes each received character as `echo: x`
- Sends `tick=... baud=...` every 3 seconds while a terminal holds DTR
- Prints the host's line coding, DTR / RTS and SEND_BREAK requests, and receive
  notifications, to the regular Serial monitor

## Usage

1. Flash the sketch and open the regular Serial monitor.
2. Connect the USB device port to the PC or host.
3. Open the newly created serial port on the PC.
4. Send characters and observe `echo: ...` replies.
5. The Serial monitor prints `CDC_LINE_CODING`, `CDC_LINE_STATE`, `CDC_BREAK`
   and `CDC_RX`.

## Key APIs

- `EspUsbDeviceCdcSerial usbSerial(device, "name")` registers the CDC ACM
  function. The optional name becomes the interface string the host shows.
- `usbSerial.configure(EspUsbBulkStreamConfig)` sets `rxCapacity`,
  `txCapacity` and the other transfer options. Only while stopped.
- `usbSerial.available()` / `read()` read bytes from the host. `read()` never
  waits.
- `usbSerial.write()` / `print()` / `printf()` send to the host. They wait for
  room up to `setTxTimeoutMs()` (default 1000 ms; 100 ms here).
- `usbSerial.flush()` starts sending and waits for the DMA and any ZLP within
  the same limit.
- `usbSerial.connected()` is "configured and DTR=1". `write()` does not require
  DTR.
- `usbSerial.lineCoding()` / `lineState()` return copies of the current values.
- `usbSerial.onLineCoding()` / `onLineState()` / `onBreak()` / `onRx()` register
  callbacks. They return `esp_err_t` and are accepted only before `begin()`.
- `usbSerial.readCapacity()` / `writeCapacity()` report what `begin()` actually
  allocated.

## Notes

- USB CDC and the logging Serial monitor are separate paths.
- The PC serial port name depends on the OS.
- Baud rate is reported as USB CDC line coding; it is not the actual USB
  transfer speed.
- Callbacks run on the USB worker task. Keep them short; do not block, call
  `device.end()` or change the configuration from them.
- `EspUsbDeviceCdcSerial` is an Arduino `Stream`, so it can be passed directly
  to code that takes `Stream&` (see [CdcConsole](../CdcConsole/)).
- Do not enable the core's USB CDC on boot; this library owns the OTG
  controller.

## See Also

- [SerialMulti](../SerialMulti/) - several CDC ports on one device
- [CdcConsole](../CdcConsole/) - CDC passed to a `Stream&` consumer
- [Keyboard](../Keyboard/) - HID keyboard device
- [CDC API](../../docs/classes/cdc.md)

## Walkthrough and checks

### Before running

Create a CDC serial function with receive and line-state diagnostics. Follow the hardware requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

`setup()` configures the port and registers every callback before `device.begin()`; any rejected step stops the sketch with a message. Open the new USB serial port and send letters plus CR/LF: this sketch replies per character, not once per input line. Compare DTR and the periodic tick output with a terminal open and closed.

### API use and adaptation

`device.ready()` concerns USB configuration; the CDC `connected()` state concerns host DTR. They answer different questions. The callback's receive amount is not a line or packet boundary: read the actual bytes and accumulate your own complete messages. Use `write()` for bytes and `print()`/`printf()` for text formatting. `flush()` is not a guarantee that the PC application consumed the data.

`write()` returns the number of bytes accepted; after a timeout, disconnect or bus reset it can be fewer than requested. Line-coding callbacks report the host's requested serial parameters; they do not change USB speed. Keep callbacks short and hand larger parsing to `loop()` through a synchronized variable. For reliable commands, check the accepted count, keep any unsent suffix, and decide what partial input means after disconnect.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one capacity, timeout or command. The demonstration's error logging is not a general retry or recovery policy.

## Changes from v2

- Ring sizes are set with `configure()` before `begin()` instead of
  `CFG_TUD_CDC_*` build flags.
- Callback registration returns `esp_err_t`; the sketch checks each result.
- `write()` / `flush()` now wait up to `setTxTimeoutMs()`. The sketch sets
  100 ms so a stalled terminal cannot hold `loop()` for a second.
- New in this example: `onBreak()`, the copied `lineCoding()` value in the
  tick, and the allocated capacities in the ready message.
- The interface has a name (`"EspUsbDevice Serial"`).

- [Sketch](Serial.ino)
- [All examples](../README.md)
- [Example workflow](../../docs/example-workflow.md)
