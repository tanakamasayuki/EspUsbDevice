# EspUsbDevice FirmwareHTTP

> 日本語版: [README.ja.md](README.ja.md)

Firmware update over USB with nothing on the host but a **browser**. The board
is a USB network adapter (CDC-NCM) with its own DHCP server, so the PC gets an
address the moment it is plugged in and can open a page on the device; the stock
`HTTPUpdateServer` upload form writes the file straight into the spare OTA
partition.

No driver to install — Windows, macOS and Linux all speak NCM natively — no host
tool, and no boot mode. Of the routes in the
[firmware update guide](../../docs/ota-over-usb.md), this is the only one where
the person doing the update needs nothing they do not already have.

## Hardware

- An ESP32-S2, ESP32-S3 or ESP32-P4 board with USB device support
- A PC as the USB host
- A separate Serial connection for logs

## Requirements

A partition scheme with **two application partitions**. The Arduino "Default"
scheme has them. The sketch prints `NO_OTA_PARTITION` when it finds only one.

## What It Does

- Brings up a CDC-NCM interface with a DHCP server on `192.168.7.0/24`
- Serves a status page at `http://192.168.7.1/`
- Serves Arduino's upload form at `http://192.168.7.1/update`
- Confirms the running image once the USB link is up

## Usage

1. Flash the sketch and open the Serial monitor.
2. Connect the USB device port to the PC. A new network interface appears and
   receives a `192.168.7.x` address.
3. Open `http://192.168.7.1/update`, pick a `.bin`, and upload.
4. The board restarts into it.

The `.bin` is a plain Arduino build: Sketch → Export Compiled Binary, or the
`build/` directory `arduino-cli compile` leaves behind.

## Key APIs

- `EspUsbDeviceNet net(device)` + `net.dhcpServer(true)` + `net.beginNetwork()`
  are the USB network device; see [`UsbNetwork`](../UsbNetwork/) for the
  addressing options.
- `HTTPUpdateServer::setup(&server, "/update")` adds the upload form. It writes
  through Arduino's `Update` library, into the same partition
  `EspUsbDeviceFirmwareUpdate` would use.
- `EspUsbDeviceFirmwareUpdate::targetLabel()` / `capacity()` report where an
  update would go, which the status page shows.
- `EspUsbDeviceFirmwareUpdate::markValid()` confirms the running image.

## Notes

- **Add credentials before this leaves a desk.** `setup(&server, "/update",
  user, password)` is the four-argument form; as written, anything that can
  reach the USB link can reflash the board.
- The image you upload replaces this sketch. Send a build that also has the NCM
  update page, or the next update has to go through
  [boot mode](../FirmwareBootMode/).
- `WebServer` listens on every interface, so the same page is reachable over
  Wi-Fi if the sketch also joins a network.
- Two boards running this on one host both default to `192.168.7.0/24`; give
  each a different `net.ipConfig(...)` subnet.
- This library is not designed to run together with Arduino's built-in USB
  device classes.
