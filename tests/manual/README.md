# Manual Tests

> 日本語版: [README.ja.md](README.ja.md)

Manual tests are reserved for behavior that cannot be fully controlled by
pytest, such as host OS enumeration dialogs, visual LED confirmation, external
USB analyzers, or physical cabling changes.

**Do not name a file in this directory `test_*.py`.** That prefix is what pytest
collects on, and everything here needs either hardware that is not permanently
attached or a person watching. A file named that way would be picked up by a
plain `pytest` run, and by `pytest manual/`, and would fail or hang for reasons
that have nothing to do with the library. The naming is the whole mechanism -
there is no marker and no `testpaths` entry backing it up, deliberately; see
[../TEST_PLAN.md](../TEST_PLAN.md).

Run these by naming the script, never through collection.

The whole diagnosis procedure is in
[docs/usb-device-guide.md](../../docs/usb-device-guide.md). The user-facing tools
that report from the device's own serial monitor live in
[`examples/Info/`](../../examples/Info/).

## `device_inspect` (descriptors as the host received them)

Purpose:

- Where `examples/Info/EspUsbDeviceDescriptorDump` shows what the device *meant*
  to send, this shows **what the host actually received**. The two must match
  byte for byte.
- Print DEVICE, CONFIGURATION (every index), DEVICE QUALIFIER, OTHER SPEED
  CONFIGURATION, BOS, string and HID report descriptors, as hex plus a
  block-by-block walk.
- Report the enumerated speed and which kernel driver bound.

Requirements:

- A board flashed with an EspUsbDevice sketch, its device connector plugged into
  this PC
- A PC where libusb is usable

Steps:

```
cd tests
uv run --with pyusb python manual/device_inspect/device_inspect.py
uv run --with pyusb python manual/device_inspect/device_inspect.py --pid 0x4051
```

Without `--pid` it reports every device with VID `0x303a`. `--json` emits a
machine-readable form, which makes before/after diffs easy:

```
uv run --with pyusb python manual/device_inspect/device_inspect.py --json > before.json
# change the descriptors and reflash
uv run --with pyusb python manual/device_inspect/device_inspect.py --json > after.json
diff -u before.json after.json
```

Expected:

- The CONFIGURATION hex matches the device-side `DescriptorDump` output.
- Device Qualifier and Other Speed Configuration are only answered when running
  at high speed.
- BOS is only present for sketches with WebUSB enabled.

Notes:

- Reading a HID report descriptor needs usbhid detached on Linux, and the
  Windows HID driver does not pass the request at all. When it cannot be read
  the reason is printed and the dump continues. Use `--no-hid` to skip it.
- For `Access denied`, apply the same fix as in the `p4_hs_bulk` section below,
  substituting the VID/PID you are using.

## `cdc_multi_ports` (every CDC port, one at a time)

Purpose:

- Confirm that **every** port of a multi-port CDC device actually carries data
  both ways, and that the ports are independent of each other.
- The automated rigs already cover this per-port: `peer/usb_serial_multi` (S3,
  two ports) and `loopback/usb_serial_multi` (P4, three ports). What this manual
  test adds is whether a PC operating system actually creates a serial node per
  port and carries the names onto them.
- Also confirms the port names (the IAD's `iFunction` / the control interface's
  `iInterface`) reached the host. Without them two ACM functions are
  indistinguishable.

You need:

- A board running [`examples/SerialMulti/`](../../examples/SerialMulti/) with its
  device connector plugged into this PC. That sketch echoes each line back
  prefixed with the port's own name, so the reply itself says which port
  received it.
- A PC with pyserial available.

Steps:

```
cd tests
uv run --with pyserial python manual/cdc_multi_ports/cdc_multi_ports.py
uv run --with pyserial python manual/cdc_multi_ports/cdc_multi_ports.py --expect 3
uv run --with pyserial python manual/cdc_multi_ports/cdc_multi_ports.py --pid 0x4018 --serial espusb-dualserial-0001
```

Expected:

- As many ports as the SoC allows (2 on S2/S3, 3 on the ESP32-P4).
- Each port's `name=` reads `Console` / `Data Link` / `Telemetry`. `(unnamed)`
  means the iInterface string did not reach the host.
- Each probe comes back prefixed with that port's name, and no two ports return
  the same reply (which would mean two nodes pointing at one function).
- The last line is `OK`.

The same script works on Windows, where the interface is read from the `MI_xx`
field of the hardware id. In Device Manager, check that each COM port is listed
under its `iFunction` name.

## `enumeration_soak` (does it survive re-enumeration)

Purpose:

- Enumerating once is not the same as staying usable. This repeats
  re-enumeration and configuration changes, checking the descriptors never drift
  and the device keeps answering.

Two cycle kinds, exercising different paths:

- `config`: `SET_CONFIGURATION 0` then `1`. The device stays addressed while
  class endpoints are torn down and rebuilt, firing `onBusDetached()` /
  `onBusAttached()`. This catches **state a class kept across a deconfigure**.
- `reset`: a real USB port reset. The device is re-addressed and re-enumerated,
  so the descriptors are rebuilt and sent again. This catches a **descriptor
  buffer that is only correct the first time**, or a controller that does not
  survive a reset.

Steps:

```
cd tests
uv run --with pyusb python manual/enumeration_soak/enumeration_soak.py --cycles 50
uv run --with pyusb python manual/enumeration_soak/enumeration_soak.py --mode reset --cycles 50
```

`--mode` defaults to `both` (alternating). `--settle-s` (10 s by default) tunes
how long to wait for the device to come back after a reset.

Expected:

- Every cycle prints `ok`, ending with
  `PASS <n> cycles, descriptors identical throughout`.
- The descriptor hex and the link speed never change from the first reading.

Notes:

- A failing cycle prints the reason (descriptor diff, timeout, never came back)
  and continues; the script exits non-zero at the end.
- A reset makes the host rebind its driver, so do not run it while the target is
  mounted as MSC or similar.

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

## `p4_hs_stream` (what limits a high-speed bulk IN)

Purpose:

- Measure one-way bulk IN throughput from an ESP32-P4 device to this PC, with
  the pattern verified so a fast run that lost data cannot look good.
- Vary the two numbers that decide it - the transmit FIFO
  (`CFG_TUD_VENDOR_TX_BUFSIZE`) and how much of that FIFO one armed transfer
  carries (`CFG_TUD_VENDOR_TX_EPSIZE`) - from `build_opt.h`, with no change to
  the library.
- Compare the spin loop against `EspUsbDeviceVendor::waitWritable()` on the same
  stream.

The second knob is the one that matters and the one nobody reaches for.
TinyUSB's vendor class submits one transfer per endpoint and re-arms it from the
completion callback, and it sizes that transfer at a single bulk packet by
default: every 512 bytes then costs a completion interrupt, an event-queue hop
and a usbd task turn. Measured on ESP32-P4 rev 1.3 over usbip, 4 MiB per run,
median of 9:

| FIFO | transfer | MB/s | note |
|-----:|---------:|-----:|------|
| 512 | 512 | 9.83 | 8.33-10.21, and 4-53 ZLP-terminated host URBs |
| 8192 | 512 | 10.76 | 10.50-11.06, no ZLP terminations |
| 8192 | 2048 | 18.64 | |
| **4096** | **4096** | **21.12** | the library's ESP32-P4 default |
| 8192 | 8192 | 22.81 | saturated; 16384 buys nothing |
| 32768 | 8192 | 23.34 | |

Requirements:

- An ESP32-P4 board with its high-speed Device connector cabled to this PC
- On WSL, `usbipd` on the Windows side
- A PC with a working libusb backend

Steps:

1. Write the arm you want to measure into `build_opt.h` next to the sketch (an
   empty file is the library's defaults), then flash:
   ```
   cd tests/manual/p4_hs_stream
   printf -- '-DCFG_TUD_VENDOR_TX_EPSIZE=8192\n' > build_opt.h
   arduino-cli compile --profile p4_hs_stream --clean
   arduino-cli upload --profile p4_hs_stream -p <port>
   ```
   `--clean` is not optional: Arduino only re-reads `build_opt.h` on a clean
   build.
2. On WSL, attach the Device connector. **Detach it again before the next
   flash** - flashing resets the chip, and a reset under a live attachment
   leaves a dead vhci entry behind that also wedges the board's serial port:
   ```
   usbipd.exe attach --wsl --busid <n>
   ```
3. Run the measurement:
   ```
   cd tests
   uv run --with pyusb python manual/p4_hs_stream/p4_hs_stream.py --runs 9
   ```

Each run prints the host's rate beside the device's own view: how many times
`write()` was refused, how many times `waitWritable()` blocked, and how many of
the host's URBs came back short. That last one is worth watching - TinyUSB sends
a zero-length packet whenever the FIFO runs dry after a transfer that was a
multiple of the packet size, which ends the host's in-flight URB early, and at a
512-byte FIFO the slow runs are exactly the runs with many of them.

## `p4_hs_hid_stream` (HID at the packet size high speed allows)

Purpose:

- Confirm that `EspUsbDeviceHidVendor` with a 511-byte report emits a 512-byte
  interrupt endpoint in the high-speed configuration and the 64 USB 2.0 allows
  in the full-speed one.
- Confirm the HID report descriptor declares Report Count 511, which is what a
  host sizes its reads from.
- Measure the rate, in order, with no gaps.

HID is the only class that needs no driver on any host OS, and at high speed it
is not the low-bandwidth class its full-speed reputation suggests: an interrupt
endpoint carries up to 1024 bytes every 125 us, and unlike bulk that bandwidth
is reserved. Measured 4.03 MB/s at 7,866 reports/s with no sequence gaps.

Requirements: as `p4_hs_stream`, plus `libusb1` for the depth sweep.

Steps:

1. Flash and attach as above, using the `p4_hs_hid_stream` profile.
2. Run the check:
   ```
   cd tests
   uv run --with pyusb python manual/p4_hs_hid_stream/p4_hs_hid_stream.py
   ```

**The rate this reports is the host's, not the device's, unless the host keeps
several URBs in flight.** One synchronous read at a time over usbip gives about
1,100 reports/s whatever the device does. The device reaches ~7,900/s - 98% of
the one-report-per-microframe ceiling - once the host submits 8 or more.

## `windows_winusb` (does Windows bind WinUSB without an .inf)

Purpose:

- Confirm that a bare vendor interface installs on Windows with no driver
  package: `USB\MS_COMP_WINUSB` in its compatible IDs and WinUSB as the bound
  service.
- Confirm the failure it replaces, by forcing the old descriptor shape.

A Microsoft OS 2.0 function subset only resolves through usbccgp.sys, which
Windows loads for composite devices only, so on a single-interface device the
subsets leave the compatible ID attached to nothing. Measured on the same board,
same firmware but for the layout flag, a fresh device instance each time:

| `msOs20Layout` | Status | Compatible IDs | Service |
|---|---|---|---|
| AUTO (flat, one interface) | OK / CM_PROB_NONE | `USB\MS_COMP_WINUSB` present | WinUSB |
| SUBSETS (the old shape) | Error / **CM_PROB_FAILED_INSTALL** | absent | none |

Requirements:

- A Windows PC (or WSL with `powershell.exe` reachable) whose Windows side can
  see the device. On WSL that means the Device connector must **not** be
  attached to WSL.

Steps:

1. **Choose a serial number that has never failed to install on this PC.**
   Windows keys a device instance on VID, PID and serial, and a failed driver
   match sticks to that instance and is never re-probed, so a serial that once
   failed reports the cached failure rather than what the descriptors now say:
   ```
   cd tests/manual/windows_winusb
   printf -- '-DWINUSB_TEST_SERIAL=\\"espusb-winusb-3\\"\n' > build_opt.h
   arduino-cli compile --profile p4_windows_winusb --clean
   arduino-cli upload --profile p4_windows_winusb -p <port>
   ```
2. Make sure the Device connector is on the Windows side (`usbipd.exe detach
   --busid <n>` if it is attached to WSL), then ask Windows:
   ```
   cd tests
   uv run python manual/windows_winusb/windows_winusb.py
   ```
3. For the control, add `-DWINUSB_TEST_LAYOUT=2` and **another** unused serial,
   and expect `CM_PROB_FAILED_INSTALL` with no `USB\MS_COMP_WINUSB`.

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
