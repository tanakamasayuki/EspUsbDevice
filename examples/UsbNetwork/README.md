# EspUsbDevice UsbNetwork

> 日本語版: [README.ja.md](README.ja.md)

Turns the board into a USB network device (CDC-NCM) with a built-in DHCP server
and a small web page. Plug it into a PC and it appears as a USB network adapter:
the PC is handed an address on `192.168.7.0/24` and can browse to
`http://192.168.7.1/`. No Wi-Fi, and no drivers to install — Windows, macOS, and
Linux support NCM natively.

This is the "USB config portal" pattern: the PC reaches a page or API served by
the device directly over USB, with zero network setup.

## Hardware

- ESP32-S3 or another Arduino-ESP32 board with USB device support
- A USB host such as a PC
- A separate Serial monitor connection for logs when available

## What It Does

- Brings up a CDC-NCM interface with `EspUsbDeviceNet`
- Runs a DHCP server so the USB host gets a `192.168.7.x` address automatically
- Serves an HTML status page (device IP, MAC, uptime, page views) at
  `http://192.168.7.1/` with the Arduino `WebServer`

## Usage

1. Flash the sketch and open the regular Serial monitor.
2. Connect the USB device port to the PC.
3. A new network interface appears and receives a `192.168.7.x` address.
4. Open `http://192.168.7.1/` in a browser.

## Key APIs

- `EspUsbDeviceNet net(device)` registers the CDC-NCM function.
- `net.ipConfig(local, gateway, subnet)` sets the interface address (default
  `192.168.7.1`).
- `net.dhcpServer(true)` runs a DHCP server (device is the gateway). Alternatives:
  `net.dhcpClient(true)` to get an address from a PC-bridged LAN, or neither for a
  static address.
- `net.beginNetwork()` brings up the lwIP/esp_netif interface. Call once after
  `device.begin()`.
- `net.localIP()` / `net.macAddress()` report the interface address and MAC.
- For raw Ethernet frames instead of an IP stack, use `net.onFrame()` /
  `net.sendFrame()` and do not call `beginNetwork()`.

## Notes

- The device side is CDC-NCM only. CDC-ECM is not enabled in the Arduino-ESP32
  core, but NCM is supported natively by modern host operating systems.
- The device having its own DHCP server is ideal for "PC talks to device"
  (config portals, local APIs). For the device to reach the internet, the PC
  would have to bridge or share its own connection, which is host-specific; use
  the ESP's own Wi-Fi for that instead.
- `WebServer` listens on all interfaces, so the same code also works over Wi-Fi
  if present.
- This library is not designed to run together with Arduino's built-in USB
  device classes.

## See Also

- [CompositeHidCdcMsc](../CompositeHidCdcMsc/) - combine multiple USB functions
- [Serial](../Serial/) - CDC ACM serial device
- `tests/manual/usb_ncm` - a minimal NCM + DHCP manual/pytest test (host ping)

## Walkthrough and checks

### Before running

Serve a local web page through a USB network interface. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

Start the USB device before `net.beginNetwork()`, then run the web server handler in `loop()`. Confirm the new host network interface and its address before opening the printed URL. USB NCM does not automatically provide Internet access, Wi-Fi, routing or a bridge to another interface.

### API use and adaptation

Check in order: USB enumeration, usable network interface, link, IP configuration, route/DNS and application connection. An IP address does not prove Internet access. Avoid changing your main PC's default route merely to test a directly connected USB subnet; inspect the selected interface and destination first.

Network callbacks and USB callbacks are separate from the application web/client loop. Keep blocking HTTP, DNS and file work outside USB callbacks. Check the return of network startup and client/server operations; decide how to reacquire state after unplugging. Record local connectivity separately from success of an external service.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](UsbNetwork.ino)
- [All examples](../README.md)
- [Example workflow](../../docs/example-workflow.md)
