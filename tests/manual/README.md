# Manual Tests

> 日本語: [README.ja.md](README.ja.md)

Manual tests are reserved for behavior that cannot be fully controlled by
pytest, such as host OS enumeration dialogs, visual LED confirmation, external
USB analyzers, or physical cabling changes.

## `usb_ncm` (USB CDC-NCM network device)

Purpose:

- Verify that the host OS enumerates the board as a CDC-NCM network adapter and
  binds its native NCM driver (no driver install).
- Verify that the device's built-in DHCP server hands the host an address on
  192.168.7.0/24.
- Verify end-to-end IP reachability (lwIP + esp_netif + the frame TX/RX glue) by
  pinging the device at 192.168.7.1.

Unlike the peer tests, this one needs the board's USB-OTG port cabled to the PC
running the tests (not the peer host board), so it is manual. The sketch, its
`sketch.yaml` (the `esp32s3` profile), and a pytest test live in
[`usb_ncm/`](usb_ncm/).

Steps:

1. Flash `usb_ncm/usb_ncm.ino` to the ESP32-S3 (or run
   `test_usb_ncm_flash_and_enumerate`, which flashes via the `esp32s3` profile
   and waits for `NCM_NET 1 ip=192.168.7.1`).
2. Cable the board's USB-OTG port to the PC.
3. Confirm the host shows a new network interface with a 192.168.7.x address.
4. Run the ping check:
   ```
   cd tests && uv run --env-file .env pytest manual/usb_ncm/test_usb_ncm.py::test_usb_ncm_ping
   ```
   Override the target with `NCM_TEST_IP` if needed.

Expected:

- Host binds an NCM/UsbNcm driver; the interface class is CDC (0x02 / NCM) with
  a CDC-Data interface.
- The host interface gets a 192.168.7.x lease.
- `ping 192.168.7.1` succeeds (0% loss).
- Device serial prints `NCM_NET 1 ...` and `rx_frames` climbs.

Notes:

- The device side is NCM only (CDC-ECM is not enabled in the Arduino-ESP32
  core). Modern Windows / macOS / Linux all support NCM natively.
- DHCP is opt-in: `net.dhcpServer(true)` (device is gateway), `net.dhcpClient(true)`
  (device gets its address from a bridged LAN — leaves room for PC-side
  bridging), or a bare `net.ipConfig(...)` for a static address with no DHCP.
- Under WSL the device's log serial may not be directly visible, but the ping
  test only needs host IP reachability, which routes through the Windows USB NIC.

## `examples/USBVendor`

Purpose:

- Verify that the host OS sees a vendor-specific interface.
- Verify bulk IN / OUT echo.
- Verify device responses to vendor control requests.
- Verify that the WebUSB BOS descriptor and landing URL are visible from a host
  or browser.

Steps:

1. Flash `examples/USBVendor` to the USB device board.
2. Open Serial monitor and wait for `USB vendor device ready`.
3. Connect the USB device port to the PC.
4. On Linux, run `lsusb -d 303a:4019 -v` and verify:
   - `bInterfaceClass 255 Vendor Specific Class`
   - bulk OUT endpoint
   - bulk IN endpoint
   - WebUSB platform capability in the BOS descriptor
5. Claim the interface from a host-side tool using libusb, WinUSB, WebUSB, or a
   similar API.
6. Send a short byte sequence to bulk OUT and verify that bulk IN returns
   `echo: ...`.
7. Send control IN request `bRequest = 0x01` and verify that it returns
   `EspUsbDeviceVendor`.
8. Send control OUT request `bRequest = 0x02` and verify that the status stage
   succeeds.
9. In a WebUSB-capable browser, select the device and verify that the landing
   URL is as expected.

Expected:

- Serial monitor prints `VENDOR_RX` and `VENDOR_CONTROL`.
- The host can open the interface with `bInterfaceClass = 0xff`.
- The bulk OUT payload matches the bulk IN echo.
- WebUSB URL is returned as `example.com/espusbdevice`.

Notes:

- Depending on the host OS, kernel driver detach, permissions, udev rules, or
  WinUSB driver binding may be required.
- `EspUsbDevice` can configure the WebUSB URL, but it does not yet expose APIs
  to replace the vendor code or Microsoft OS 2.0 descriptor contents.
- The Arduino-ESP32 TinyUSB core also returns a Microsoft OS 2.0 descriptor when
  WebUSB is enabled. GUIDs and other contents are the core defaults.
- This belongs in manual testing because it depends on the host OS, browser, and
  driver state.

## `examples/MSCFatRamDisk`

Purpose:

- Verify that the host OS can mount the `EspUsbDeviceMscFatRamDisk` FAT12 RAM
  disk.
- Copy `CONFIG.TXT` from the host and verify that the device can read it after
  eject / unmount.

Steps:

1. Flash `examples/MSCFatRamDisk` to the USB device board.
2. Open Serial monitor and wait for `USB FAT RAM disk ready`.
3. Connect the USB device port to the PC.
4. Verify that the `ESPUSB` drive appears on the PC.
5. Copy `CONFIG.TXT` to the drive root.
6. Eject or unmount the drive from the OS.
7. Verify that Serial monitor prints `MSC_EJECT`, `CONFIG_SIZE`, and
   `CONFIG_BEGIN` / `CONFIG_END`.

Expected:

- Initial file `README.TXT` is visible on the host.
- `CONFIG.TXT` content is printed on Serial.
- The ESP32 side does not scan files before eject.

Notes:

- RAM disk contents are lost on reset or power cycle.
- If the host OS asks to format the drive, it may not accept this small FAT12
  image.
- Do not read the FAT image on the ESP32 side while the host may still be
  writing it.
- Large firmware images should use PSRAM, SD card, or streaming update instead
  of this small example.

## `examples/MSCSdCard`

Purpose:

- Verify that an SPI-connected SD card can be read/written by the host OS
  through USB MSC.
- Verify that ownership can return to the device side after host eject /
  unmount.

Steps:

1. Change `SD_CS_PIN` in `examples/MSCSdCard/MSCSdCard.ino` for the board.
2. Insert an SD card. Back it up first if needed because the host can modify it.
3. Flash `examples/MSCSdCard` to the USB device board.
4. Open Serial monitor and wait for `USB SD MSC ready`.
5. Connect the USB device port to the PC.
6. Verify that the SD card appears as USB storage on the PC.
7. Create, read back, and delete a small test file.
8. Eject or unmount the drive from the OS.
9. Verify that Serial monitor prints `SD_EJECT`.

Expected:

- The host can mount the SD card's existing FAT filesystem.
- Host writes are reflected on the SD card.
- ESP32-side file APIs such as `SD.open()` are not used before eject.

Notes:

- Concurrent writes from the host and ESP32 side can corrupt the SD filesystem.
- This example calls `SD.begin()`, which also mounts the Arduino-side
  filesystem, but file APIs are intentionally avoided while MSC owns the card.
- SD socket, CS pin, and SPI pins vary by board.
