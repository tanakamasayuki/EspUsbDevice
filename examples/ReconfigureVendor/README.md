# EspUsbDevice ReconfigureVendor

> 日本語版: [README.ja.md](README.ja.md)

Changes a Function's buffer sizes while the sketch keeps running:
`device.end()` → `configure()` → `device.begin()`.

Capacities in v3 are run-time settings of each Function. `begin()` allocates
them and `end()` releases them, so a different size needs neither a rebuild nor
a reset. The host sees the change as an unplug followed by a new plug.

## Hardware

- ESP32-S2 / S3 / P4 board with a native USB OTG port
- A USB host that can open a vendor interface (libusb / pyusb / WinUSB, or
  another ESP32 running EspUsbHost)
- A separate Serial monitor connection (the UART / USB Serial-JTAG side used for
  flashing). It is used both for logs and for the `r` command.

## What It Does

- Starts a vendor bulk function with 128-byte receive and send rings
  (`transferBytes = 64`) and prints `Started: RX=128 TX=128`
- Echoes bulk OUT data back on bulk IN, keeping any unsent rest as in
  [VendorEcho](../VendorEcho/)
- When `r` arrives on the Serial monitor, calls `device.end()`, switches the
  ring size between 128 and 2048 bytes, configures again and calls `begin()`.
  The new effective sizes are printed.
- Prints `esp_err_to_name()` of a failed `configure()` or `end()`, and
  `device.lastErrorName()` of a failed `begin()`

## Usage

1. Flash the sketch and open the Serial monitor at 115200.
2. Connect the USB device port to the host. Check the echo with the pyusb script
   in [VendorEcho](../VendorEcho/) (same VID `0x303a` / PID `0x4000`).
3. Send `r` from the Serial monitor. `Started: RX=2048 TX=2048` appears.
4. On the host the device disconnects and enumerates again. On Linux,
   `dmesg -w` shows the disconnect and a new device number. Open the device
   again on the host; the old handle is gone.
5. Send `r` again to return to 128 bytes.

## Key APIs

- `device.end()` stops USB, waits for the worker and DMA, and releases what
  `begin()` allocated. It returns `esp_err_t`. It waits up to
  `EspUsbDeviceConfig::stopTimeoutMs` (default 5000 ms, 0 = no limit). On
  `ESP_ERR_TIMEOUT` the stop request stays pending and a later `end()` finishes
  it. Calling it from a USB callback returns `ESP_ERR_INVALID_STATE`.
- `vendor.configure(EspUsbVendorConfig)` is accepted only while stopped. It
  returns `ESP_ERR_INVALID_STATE` while running.
- `device.begin()` allocates the rings again. On failure
  `device.lastErrorName()` gives the `esp_err_to_name()` name.
- `vendor.readCapacity()` / `writeCapacity()` report what `begin()` actually
  allocated. They are 0 while stopped.
- `vendor.read()` / `write()` never wait, as in VendorEcho.

## Notes

- Data still in the rings at `end()` is discarded. The sketch also clears its
  held chunk (`length = sent = 0`).
- The host cannot tell this from a pulled cable. Host software must reopen the
  device after re-enumeration.
- The `r` command uses the Serial monitor, not the vendor interface, because
  the vendor interface disappears during the switch.
- The same order works for every Function and for adding or changing settings
  such as the identity in `EspUsbDeviceConfig`.

## See Also

- [VendorEcho](../VendorEcho/) - the same echo without reconfiguration
- [USBVendor](../USBVendor/) - vendor requests and WebUSB
- [Vendor API](../../docs/classes/vendor.md)
- [USB device guide](../../docs/usb-device-guide.md) - configure, begin, end

## Walkthrough and checks

### Before running

Follow the hardware requirements above and select the board and the example's
profile. [Example workflow](../../docs/example-workflow.md) explains
installation, connectors, USB roles and the difference between startup and a
usable peer.

### Follow the code

`start()` is the whole start sequence: `configure()`, then `device.begin()`,
then a print of the effective sizes. `loop()` calls it again only after `end()`
returned `ESP_OK`. If `end()` fails, the sketch prints the reason and keeps the
current state; sending `r` again retries `end()`.

### API use and adaptation

Treat `end()` as a disconnect: stop host-facing work first, and do not call it
from a USB callback. Check `configure()` and `begin()` separately, because they
fail for different reasons (a bad value, or a resource that does not fit).

### Verify one change at a time

Check the echo at 128 bytes, switch once, and check it again at 2048 bytes
before repeating the switch many times.

## Changes from v2

This example is new in v3. v2 has no corresponding example.

- [Sketch](ReconfigureVendor.ino)
- [All examples](../README.md)
- [Example workflow](../../docs/example-workflow.md)
