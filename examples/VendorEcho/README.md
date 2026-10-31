# EspUsbDevice VendorEcho

> 日本語版: [README.ja.md](README.ja.md)

The smallest vendor-specific bulk device: every byte the host writes to bulk
OUT comes back on bulk IN.

The point of the example is the send loop. `EspUsbDeviceVendor::write()` never
waits; it copies what fits into the send ring and returns that count. The sketch
keeps the unsent rest of a chunk and retries it on the next `loop()`, so nothing
is dropped when the host reads slowly.

## Hardware

- ESP32-S2 / S3 / P4 board with a native USB OTG port
- A USB host that can open a vendor interface: a PC with libusb / pyusb /
  WinUSB, or another ESP32 running EspUsbHost
- No Serial monitor output is used

## What It Does

- Configures the vendor function with 256-byte receive and send rings and
  `transferBytes = 64`, then starts `EspUsbDevice` with the default identity
  (VID `0x303a`, PID `0x4000`)
- While the host has the device configured, reads up to 64 bytes into
  `pending`, writes them back, and keeps `length` / `sent` until the whole chunk
  is accepted
- Drops the held chunk when the host goes away (`device.ready()` becomes false)

## Checking From the Host

The device has one interface with `bInterfaceClass = 0xff` and one bulk OUT /
bulk IN pair. Read the endpoint addresses from the descriptor instead of
hard-coding them.

```python
# pyusb (libusb backend)
import usb.core, usb.util
dev = usb.core.find(idVendor=0x303a, idProduct=0x4000)
intf = dev.get_active_configuration()[(0, 0)]
def ep(direction):
    return usb.util.find_descriptor(intf, custom_match=lambda e:
        usb.util.endpoint_direction(e.bEndpointAddress) == direction)
ep(usb.util.ENDPOINT_OUT).write(b"hello vendor")
print(bytes(ep(usb.util.ENDPOINT_IN).read(512, timeout=1000)))
```

- Linux: no kernel driver binds. Run as root or add a udev rule for the VID/PID.
- Windows: the library's Microsoft OS 2.0 descriptor binds WinUSB without an
  `.inf`. This has not been checked on Windows with this example.
- macOS: libusb can open the interface. Not checked.
- Read with a length that is a multiple of the bulk packet size (64 at full
  speed, 512 at high speed), as in the script above.

## Key APIs

- `EspUsbDeviceVendor vendor(device)` registers the vendor function. The
  optional second argument is the full-speed bulk packet size: 8, 16, 32 or 64
  (USB 2.0 §5.8.3). Other values make `device.begin()` fail with
  `ESP_ERR_INVALID_SIZE`. High speed always uses 512.
- `vendor.configure(EspUsbVendorConfig)` sets `rxCapacity`, `txCapacity`,
  `transferBytes` and the receive mode. Only while stopped.
- `vendor.read(buffer, size)` copies received bytes and never waits.
- `vendor.write(buffer, size)` copies what fits into the send ring and returns
  the count. It never waits. It returns 0 while the host has not configured the
  device.
- `device.ready()` is true while the host has the device configured.

## Notes

- `transferBytes = 64` is the most one IN submission carries. On the P4
  high-speed controller (bulk packet 512) each submission is a short packet.
  Raise it to 512 or more for throughput.
- Bulk is a byte stream. Several host writes can arrive in one `read()`, and
  one write can arrive in several. Frame application messages with a length.
- The default for Vendor is no ZLP after a send that ends on a packet boundary.
  Do not make the host wait for a short packet.
- If the host writes more than the receive and send rings hold (about 512 bytes here)
  before it reads, both sides wait. Read and write in parallel on the host.
- A failed `configure()` or `begin()` is printed on `Serial` (115200) of the
  separate debug connection, with the error name.

## See Also

- [USBVendor](../USBVendor/) - vendor requests, WebUSB and a logged echo
- [ReconfigureVendor](../ReconfigureVendor/) - the same echo with ring sizes
  changed at run time
- [MidiEcho](../MidiEcho/) - the same hold-and-retry pattern for MIDI packets
- [Vendor API](../../docs/classes/vendor.md)

## Walkthrough and checks

### Before running

Follow the hardware requirements above and select the board and the example's
profile. [Example workflow](../../docs/example-workflow.md) explains
installation, connectors, USB roles and the difference between startup and a
usable peer.

### Follow the code

`loop()` holds at most one 64-byte chunk. It reads a new chunk only after the
previous one is fully accepted, so the receive ring fills up when the host stops
reading, and the device stops taking OUT data. That is the backpressure path.

### API use and adaptation

USB IN/OUT is named from the host: IN goes to the host, OUT comes from it.
A completed `write()` means the bytes were copied, not that the host read them.
Keep the "unsent rest" logic when you change the chunk size or ring sizes.

### Verify one change at a time

Run the unchanged example first and compare the echo with what you sent. Then
change one value (ring size, `transferBytes`, chunk size) and repeat.

## Changes from v2

This example is new in v3. v2 has no corresponding example.

- [Sketch](VendorEcho.ino)
- [All examples](../README.md)
- [Example workflow](../../docs/example-workflow.md)
