# EspUsbDevice

> 日本語: [README.ja.md](README.ja.md)

EspUsbDevice is a USB Device library for ESP32 Arduino in which you state what
the device is. Attach the Functions you need to a Device, set their capacities,
and call `begin()`. The library runs its own USB worker, so the sketch never
polls a USB task, and only the Functions you use are linked.

```cpp
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceCdcSerial console(device);   // a USB serial port
EspUsbDeviceHidKeyboard keyboard(device); // and a keyboard, in one device

void setup() {
  EspUsbBulkStreamConfig buffers;
  buffers.rxCapacity = 256;
  buffers.txCapacity = 1024;
  console.configure(buffers);
  if (!device.begin()) {
    // device.lastErrorName() says why, e.g. "ESP_ERR_INVALID_SIZE"
  }
}

void loop() {
  if (device.ready() && console.available()) {
    const int c = console.read();
    console.write(uint8_t(c));
    if (c == '!') keyboard.tapKey('a');
  }
  delay(1);
}
```

## Supported chips and classes

| Chip | Speed | [HID](docs/classes/hid.md) | [CDC](docs/classes/cdc.md) | [Vendor](docs/classes/vendor.md) | [MIDI](docs/classes/midi.md) | [MSC](docs/classes/msc.md) | [Audio](docs/classes/audio.md) | [Video](docs/classes/video.md) | [NCM](docs/classes/ncm.md) | [CCID](docs/classes/ccid.md) | [DFU](docs/classes/dfu.md) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| ESP32-S3 | FS (12 Mbps) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| ESP32-P4 v1.x and P4X (v3.1+) | HS (480 Mbps) and FS | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| ESP32-S2 | FS (12 Mbps) | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ |

✅ = transfers checked on hardware by the `validation/` probes: two ESP32-S3
boards at full speed; a P4 and a P4X linked at high speed in both directions
and one P4 linking its own HS and FS controllers; a P4X against a Linux PC.
○ = builds; not yet run on hardware. ESP32-S31 is not supported yet.

Classes: HID (keyboard with NKRO, mouse, gamepad, consumer and system control,
custom and vendor reports), CDC ACM, vendor bulk and control (WebUSB, WinUSB),
MIDI 1.0, MSC (RAM, FAT, SD card and firmware disks), UAC1/UAC2 audio, UVC
video, CDC-NCM networking, CCID smart card reader and DFU 1.1. They combine
freely into one composite device; the only limits are the controller's
endpoints and FIFO, and a configuration that does not fit fails `begin()`.

## Requirements

- Arduino-ESP32 **3.3.12** (the examples pin it in `sketch.yaml`).
- ESP32-S3 and ESP32-S2: set **Tools > USB Mode** to **USB-OTG (TinyUSB)**
  (`USBMode=default`); the ESP32-S3 default is Hardware CDC and JTAG.
- ESP32-P4: choose the chip revision in **Tools > Chip Variant**: v1.x is
  `prev3`, v3.1 or later (P4X) is `postv3`.
- Do not enable the core's own USB CDC, MSC or DFU on boot, and do not call
  `USB.begin()`: this library owns the USB OTG controller.

## Highlights

- **Explicit and pay-for-what-you-use.** Capacities, transfer modes and buffers
  are per instance and set at run time with `configure()`; no `build_opt.h`.
  `begin()` allocates, `end()` frees, and a failure leaves nothing behind.
- **Ownership you can rely on.** Copy, callback and borrowed-DMA transfers;
  every borrowed buffer comes back exactly once, also on cancel, bus reset or
  `end()`. Rings and storage can live in PSRAM.
- **High speed on ESP32-P4.** 512-byte bulk packets, 1024-byte interrupt
  packets, isochronous audio and video at 480 Mbps, `device.speed()` and
  TEST_MODE.
- **Standards first.** Descriptors, control requests and class behaviour follow
  USB 2.0 and the class specifications; where the library had to choose, the
  [class guides](docs/classes/) give the clause.
- **Diagnosable.** `begin()` checks the endpoint and FIFO budget before touching
  the bus, and `device.lastErrorName()` names the reason.

## Documentation

| Read | For |
| --- | --- |
| [USB device guide](docs/usb-device-guide.md) | USB basics, power and connectors, ESP32 specifics, bring-up |
| [Class guides](docs/classes/) | One per class: usage, every setting, behaviour, host OS notes |
| [Advanced guide](docs/usb-device-advanced.md) | Task model, descriptors byte by byte, control transfers, FIFO, DMA and PSRAM |
| [Examples](examples/README.md) | 40+ complete sketches, every 2.x example included |
| [Troubleshooting](docs/troubleshooting.md) | `begin()` errors, enumeration problems, per-OS and per-class issues |
| [OTA over USB](docs/ota-over-usb.md) | Firmware update routes: ROM loader, DFU, drive, serial, vendor, browser |
| [Migrating from v2](docs/migrating-from-v2.md) | Moving an EspUsbDevice 2.x sketch to 3.0 |
| [Migrating from Arduino-ESP32 USB](docs/migrating-from-arduino-esp32-usb.md) | Moving from `USBHIDKeyboard`, `USBCDC`, `USBMSC` and friends |
| [Changelog](CHANGELOG.md) | What changed in each version |

## Not yet verified

Windows and macOS hosts, ESP32-S2 hardware, real SD cards and OTA writes on
hardware, and Arduino-ESP32 4.x. The library is tested against ESP32 hosts and
a Linux PC; see [testing](docs/development/testing.ja.md).

## License

MIT; see [LICENSE](LICENSE). The DWC2 controller driver is derived from TinyUSB
(MIT) and ST code (BSD-3-Clause); see [third_party/tinyusb](third_party/tinyusb/PROVENANCE.md).
