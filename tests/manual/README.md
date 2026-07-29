# Manual Tests

> 日本語: [README.ja.md](README.ja.md)

Manual tests are reserved for behavior that cannot be fully controlled by
pytest, such as host OS enumeration dialogs, visual LED confirmation, external
USB analyzers, or physical cabling changes.

## `p4_hs_bulk` (ESP32-P4 High-Speed Device)

Purpose:

- Connect the ESP32-P4 HS Device controller directly to a PC and verify USB
  High-Speed enumeration (480 Mbit/s signaling).
- Verify bulk endpoint MPS 512 in the active HS configuration and MPS 64 in the
  Full-Speed Other-Speed Configuration.
- Retrieve the Device Qualifier.
- Run sustained raw bulk OUT/IN echo and detect timeouts, short transfers, or
  data corruption.

Requirements:

- An ESP32-P4 board with an external UTMI HS PHY and its Device connector
- A data-capable USB cable
- A PC with a working libusb backend

Steps:

1. Flash [`p4_hs_bulk/p4_hs_bulk.ino`](p4_hs_bulk/p4_hs_bulk.ino):
   ```
   cd tests/manual/p4_hs_bulk
   arduino-cli compile --profile esp32p4 --upload
   ```
2. Wait for `P4_HS_BULK_READY` on the serial monitor.
3. Check the board schematic and connect the Device connector wired to the
   external UTMI HS PHY to the PC. It is not the USB Serial/JTAG connector or
   the GPIO26/GPIO27 FS pair.
4. On Linux, optionally run `lsusb -t` and confirm that the link shows `480M`.
5. Run the host check:
   ```
   cd tests
   uv run --with pyusb python manual/p4_hs_bulk/p4_hs_bulk.py --megabytes 16
   ```
   For a longer run, increase the amount, for example to `--megabytes 256`.

If Linux or WSL reports `Access denied (insufficient permissions)`, grant
temporary access to the current connection and rerun the check (replace
`001/010` with the node printed by the checker):

```
sudo chmod a+rw /dev/bus/usb/001/010
```

For persistent access, install a udev rule:

```
echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="303a", ATTR{idProduct}=="4041", MODE="0660", GROUP="plugdev"' \
  | sudo tee /etc/udev/rules.d/70-espusbdevice-p4-hs.rules
sudo udevadm control --reload-rules
sudo udevadm trigger --attr-match=idVendor=303a --attr-match=idProduct=4041
```

Reconnect the USB device afterward. If usbipd passes the device through to WSL,
detach and attach it again. The `/dev/bus/usb/BBB/DDD` numbers can change on
each connection, while the udev rule follows the VID/PID.

Pass criteria:

- `PASS link: USB High-Speed`.
- The active descriptor has bulk IN/OUT MPS 512.
- The Device Qualifier can be retrieved.
- The Other-Speed Configuration has bulk IN/OUT MPS 64.
- Every requested byte echoes correctly and the script exits with
  `PASS bulk echo`.
- Device log `P4_HS_BULK_STATUS` remains at `errors=0` with no unexpected
  reboot.

Notes:

- PyUSB needs a libusb backend and permission to access the device. Running
  directly on Windows may require WinUSB binding.
- The reported MiB/s includes one synchronous echo per packet. It is a
  stability check, not a maximum-throughput benchmark.
- Flushing an echo of exactly 512 bytes makes TinyUSB terminate the transfer
  with a ZLP. The checker counts and skips this valid zero-length packet before
  comparing the complete echo payload.
- An interrupted run can leave an echo or ZLP in an endpoint/FIFO. Before
  comparing new payloads, the checker reinitializes class endpoints with the
  standard USB `SET_CONFIGURATION 0 → 1` sequence.
- Physical HS cable/port/PHY conditions make this a release-candidate manual
  test rather than part of the default pytest suite.

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
- `EspUsbDevice` generates the WebUSB and Microsoft OS 2.0 descriptors, but
  does not yet expose APIs to replace their vendor codes, GUID, or contents.
- Descriptor bytes and the vendor control response are automated. Actual
  browser behavior and Windows driver binding remain manual because they depend
  on host OS, browser, and driver state.

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
