# EspUsbDevice FirmwareHTTP

> 日本語版: [README.ja.md](README.ja.md)

Firmware update over USB with nothing on the host but a **browser**. The board
is a USB network adapter (CDC-NCM) with its own DHCP server, so the PC gets an
address the moment it is plugged in and can open a page on the device; the
stock `HTTPUpdateServer` upload form writes the file straight into the spare OTA
partition.

No driver to install - Windows, macOS and Linux all speak NCM natively - no host
tool, and no boot mode. Of the routes in the
[firmware update guide](../../docs/ota-over-usb.md), this is the only one where
the person doing the update needs nothing they do not already have.

## Hardware

- An ESP32-S2, ESP32-S3 or ESP32-P4 board, connected through its **native USB OTG** port
- A PC as the USB host
- A separate Serial connection for logs

## Requirements

A partition scheme with **two application partitions**. The Arduino "Default"
scheme, used by every profile in `sketch.yaml`, has them. The sketch prints
`NO_OTA_PARTITION` when it finds only one. Keep a recovery route (the ROM
loader) available before testing.

## What It Does

- Brings up a CDC-NCM interface named "Firmware update" with a DHCP server on
  `192.168.7.0/24` (device `192.168.7.1`)
- Serves a status page at `http://192.168.7.1/` (running image, update target,
  room, uptime)
- Serves Arduino's upload form at `http://192.168.7.1/update`
- Confirms the running image once the USB network is up

## Usage

1. Flash the sketch and open the Serial monitor at 115200.
2. Connect the native USB port to the PC. A new network interface appears and
   receives a `192.168.7.x` address (`ip addr` / `ipconfig` shows it).
3. Open `http://192.168.7.1/update`, pick a `.bin` (Firmware), and upload.
   Or from a terminal:
   `curl -F "firmware=@build/FirmwareHTTP.ino.bin" http://192.168.7.1/update`
4. The page reports success and the board restarts into the new image.

The `.bin` is a plain Arduino build: Sketch > Export Compiled Binary, or
`<build-path>/<sketch>.ino.bin` from `arduino-cli compile` - not
`*.merged.bin`.

## Key APIs

- `EspUsbDeviceNet net(device)` registers the CDC-NCM function;
  `net.configure(EspUsbNetConfig)` sets its name and buffer sizes (defaults
  are fine for IP).
- `net.dhcpServer(true)` before `device.begin()`; `net.beginNetwork()` after
  it, from the sketch task. `net.ipConfig(local, gateway, subnet)` changes the
  `192.168.7.1/24` default. See the [NCM guide](../../docs/classes/ncm.md).
- `HTTPUpdateServer::setup(&server, "/update")` adds the upload form. It writes
  through Arduino's `Update` library, into the same partition
  `EspUsbDeviceFirmwareUpdate` would use.
- `EspUsbDeviceFirmwareUpdate::targetLabel()` / `capacity()` report where an
  update would go, which the status page shows.
- `EspUsbDeviceFirmwareUpdate::markValid()` confirms the running image.

## Expected Serial Output

```text
Ready at http://192.168.7.1/update
```

## Notes

- **Add credentials before this leaves a desk.**
  `updater.setup(&server, "/update", user, password)` is the four-argument
  form; as written, anything that can reach the USB link can reflash the
  board. `markValid()` is not an authenticity check either.
- The image you upload replaces this sketch. Send a build that also has the
  NCM update page, or the next update has to go through
  [boot mode](../FirmwareBootMode/).
- The DHCP server does not advertise a gateway or DNS, so the PC's own internet
  route is untouched.
- `WebServer` listens on every interface, so the same page is reachable over
  Wi-Fi if the sketch also joins a network.
- Two boards running this on one host both default to `192.168.7.0/24`; give
  each a different `net.ipConfig(...)` subnet.
- Ordinary firmware replacement keeps NVS.
- Do not enable Arduino's built-in USB classes (USB MSC / DFU / CDC on boot)
  together with this library.

## Checks

Bring up NCM and confirm host-side IP connectivity (`ping 192.168.7.1`) before
opening the updater page. USB enumeration, the IP address, the upload, image
verification and the next boot are separate stages. `WebServer` and
`HTTPUpdateServer` run in `loop()`, not in a USB callback.

## Changes from v2

- `net.configure()` names the interface ("Firmware update"); buffer sizes are
  run-time `EspUsbNetConfig` fields rather than build-time macros.
- `dhcpServer()` and the other NCM setters return `esp_err_t`.
- Startup failures also print `net.startError()`.
- The README adds a `curl` upload command. Addressing, pages and update flow
  are unchanged.

## See Also

- [FirmwareDFU](../FirmwareDFU/), [FirmwareMSC](../FirmwareMSC/),
  [FirmwareCDC](../FirmwareCDC/), [FirmwareVendor](../FirmwareVendor/) - other
  update routes
- [FirmwareBootMode](../FirmwareBootMode/) - the ROM loader recovery path
- [Firmware update over USB](../../docs/ota-over-usb.md)
- [NCM API guide](../../docs/classes/ncm.md)
- [Sketch](FirmwareHTTP.ino) / [All examples](../README.md) /
  [Example workflow](../../docs/example-workflow.md)
