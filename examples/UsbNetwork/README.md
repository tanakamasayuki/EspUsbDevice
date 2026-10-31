# EspUsbDevice UsbNetwork

> 日本語版: [README.ja.md](README.ja.md)

Turns the board into a USB network device (CDC-NCM) with a built-in DHCP server
and a small web page. Plug it into a PC and it appears as a USB network adapter:
the PC is handed an address on `192.168.7.0/24` and can browse to
`http://192.168.7.1/`. No Wi-Fi, and no drivers to install - Windows, macOS and
Linux support NCM natively.

This is the "USB config portal" pattern: the PC reaches a page or API served by
the device directly over USB, with zero network setup.

```cpp
EspUsbDevice device;
EspUsbDeviceNet net(device);

EspUsbNetConfig config;            // NTB sizes and buffer counts
config.name = "EspUsbDevice NCM";
net.configure(config);
net.ipConfig(IPAddress(192, 168, 7, 1), IPAddress(192, 168, 7, 1), IPAddress(255, 255, 255, 0));
net.dhcpServer(true);
device.begin(identity);
net.beginNetwork();                // esp_netif + lwIP, after device.begin()
```

## Hardware

- ESP32-S3 or another Arduino-ESP32 board with USB device support (ESP32-S2,
  ESP32-P4)
- A USB host such as a PC
- A separate Serial monitor connection for logs when available

## What It Does

- Brings up a CDC-NCM interface with `EspUsbDeviceNet`
- Starts the ESP-IDF network stack (esp_netif / lwIP) on it with
  `beginNetwork()`
- Runs a DHCP server so the USB host gets a `192.168.7.x` address automatically
- Serves an HTML status page (device IP, MAC, uptime, page views, USB frame
  counts) at `http://192.168.7.1/` with the Arduino `WebServer`
- Prints the USB link going up and down

## Usage

1. Flash the sketch and open the regular Serial monitor.
2. Connect the USB device port to the PC.
3. A new network interface appears and receives a `192.168.7.x` address.
4. Open `http://192.168.7.1/` in a browser.

## Key APIs

- `EspUsbDeviceNet net(device)` registers the CDC-NCM function.
- `net.configure(config)` sets the NTB sizes (`ntbInBytes`, `ntbOutBytes`,
  default 3200), buffer counts (`txBuffers` 3, `rxBuffers` 2), datagrams per
  NTB and the interface name. The buffers are allocated at `device.begin()`.
  The IP stack needs `maxFrameBytes` >= 1514 and at least one buffer each way.
- `net.ipConfig(local, gateway, subnet)` sets the interface address (default
  `192.168.7.1`).
- `net.dhcpServer(true)` runs a DHCP server. Alternatives: `net.dhcpClient(true)`
  to get an address from a PC-bridged LAN, or neither for a static address.
- The DHCP server does not advertise a gateway or DNS by default, so the PC
  keeps its own default route. `net.dhcpAdvertiseGateway(true)` and
  `net.dhcpDns(ip)` offer them, for a device that actually forwards traffic.
  `net.defaultRoute(true)` makes the USB link the ESP's own default route.
- `net.beginNetwork()` brings up the esp_netif / lwIP interface. Call it from
  the sketch task after `device.begin()`; `net.endNetwork()` stops only the IP
  layer.
- `net.localIP()` / `net.macAddress()` report the interface address and the MAC
  sent to the host; `net.linkUp()` is the USB data interface state and
  `net.stats()` counts frames and rejected NTBs.
- For raw Ethernet frames instead of an IP stack, use `net.onFrame()` /
  `net.sendFrame()` and do not call `beginNetwork()`.

## Notes

- The device side is CDC-NCM only. CDC-ECM is not provided, but NCM is
  supported natively by modern host operating systems.
- The device having its own DHCP server is ideal for "PC talks to device"
  (config portals, local APIs). For the device to reach the internet, the PC
  would have to bridge or share its own connection, which is host-specific; use
  the ESP's own Wi-Fi for that instead.
- `WebServer` listens on all interfaces, so the same code also works over Wi-Fi
  if present.
- The receive buffers are rounded up to the bulk packet (64 bytes at full
  speed, 512 at high speed), so a P4 on its high-speed port uses slightly more
  RAM for the same `ntbOutBytes`.
- This library is not designed to run together with Arduino's built-in USB
  device classes.

## See Also

- [CompositeHidCdcMsc](../CompositeHidCdcMsc/) - combine multiple USB functions
- [CdcConsole](../CdcConsole/) - CDC ACM serial device
- [v2 tests/manual/usb_ncm](https://github.com/tanakamasayuki/EspUsbDevice/tree/3fed50d0d777f600432b71381faccd93adb9a202/tests/manual/usb_ncm/) - the
  v2 minimal NCM + DHCP manual test (host ping)

## Walkthrough and checks

### Before running

Select the board's profile (`esp32s3` is the default; `esp32s2`, `esp32p4`
for P4 v1.x and `esp32p4x` for P4 v3.1 or newer; v3.0 is not supported), and locate the
diagnostic Serial connection. [Example workflow](../../docs/example-workflow.md)
explains installation, connectors, USB roles and the difference between
startup and a usable peer.

### Follow the code

Start the USB device before `net.beginNetwork()`, then run the web server
handler in `loop()`. Confirm the new host network interface and its address
before opening the printed URL. USB NCM does not automatically provide
Internet access, Wi-Fi, routing or a bridge to another interface.

### API use and adaptation

Check in order: USB enumeration, USB link (`linkUp()`), IP configuration,
route/DNS and the application connection. An IP address does not prove
Internet access. Avoid changing your main PC's default route merely to test a
directly connected USB subnet.

`onFrame` runs on the USB worker and is separate from the web server loop.
Keep blocking HTTP, DNS and file work out of USB callbacks. Check the return
of network startup and decide how to recover state after unplugging.

### Verify one change at a time

Run the unchanged example first and compare its log with the host's network
settings. Change one address or size at a time. The error printing is not a
retry or recovery policy.

## Changes from v2

- NTB sizes and buffer counts are set per Function with `net.configure()` and
  allocated at `begin()`, replacing the fixed NTB pool and transmit queue.
- `ipConfig()`, `dhcpServer()` and the other setters return `esp_err_t`; the
  sketch checks them.
- The page also shows USB frame counts from `net.stats()`, and the sketch
  prints the USB link state.

Compile-checked with Arduino-ESP32 3.3.12 for every profile in
`sketch.yaml`; this sketch itself has not been run on hardware.

- [Sketch](UsbNetwork.ino)
- [All examples](../README.md)
- [NCM API](../../docs/classes/ncm.md)
- [Example workflow](../../docs/example-workflow.md)
