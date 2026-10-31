# EspUsbDevice

> 日本語: [README.ja.md](README.ja.md)

EspUsbDevice is an explicit USB Device library for ESP32 Arduino. Attach the
Functions you need to a Device, configure their resources, then call `begin()`.
The library starts its own USB worker; sketches do not call a USB polling task.

This is the **v3 development branch**, with metadata version 3.0.0. No v3 release
or tag has been published. v3 replaces the TinyUSB class/core backend and moves
buffer capacities and transfer modes into per-instance APIs. It intentionally
breaks some 2.x APIs; see the [migration guide](docs/V3_MIGRATION.ja.md).

```cpp
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceCdcSerial console(device);

void setup() {
  EspUsbBulkStreamConfig buffers;
  buffers.rxCapacity = 256;
  buffers.txCapacity = 512;
  buffers.transferBytes = 64;
  if (console.configure(buffers) != ESP_OK) return;
  if (!device.begin()) return;
}

void loop() {
  while (console.available() && console.writeAvailable()) {
    const int value = console.read();
    if (value >= 0) console.write(uint8_t(value));
  }
  delay(1);
}
```

`begin()` reserves resources and starts USB. `started()` reports that lifecycle;
`ready()` reports configuration by the Host. Change resources after a successful
`end()`, then configure and begin again. The Host sees disconnection and
re-enumeration. No `build_opt.h` is needed for Function capacity or instance count.

## Design

- Select Functions explicitly. Unused class implementations are not pulled into
  the link through a global driver table, and unused class buffers are not
  reserved for maximum instance counts.
- Allocate selected resources at startup, with rollback on failure. Normal
  transfers do not repeatedly allocate heap storage. External buffers and
  borrowed DMA transfers are available where the class supports them.
- Control descriptors, port, speed, endpoint packet sizes and raw reports.
  Hardware endpoint, FIFO and bandwidth limits still apply.
- Own the class drivers, common core and controller boundary. The DWC2 driver is
  a maintained fork with its [origin and licenses](third_party/tinyusb/PROVENANCE.md)
  retained. There is no vendored TinyUSB stack/configuration or SDK stack fallback.
- Use Arduino-ESP32/ESP-IDF for the platform, PHY, FreeRTOS and memory/cache APIs.
  Standard Arduino USB Device startup must not run alongside this library.
  Network, storage and firmware helpers remain optional; Audio does not require
  an I2S/codec implementation.

HID keyboard/mouse/gamepad/consumer/system/custom reports, CDC ACM, MIDI 1.0,
Vendor bulk/control, MSC, UAC1/UAC2 Audio, UVC Video, CDC-NCM, CCID and DFU are
implemented. Combine Functions within the selected controller's endpoint budget;
there is no library-wide four-Function limit. Implementation coverage and
hardware qualification are tracked separately; see the hardware status below.

## Build and validation

The development baseline is **Arduino-ESP32 3.3.12**. Examples pin that version
in `sketch.yaml`. Older-core compatibility reports describe earlier library
versions; they do not qualify v3.

### Hardware status by target (2026-10-31)

| Target | On hardware | Main coverage |
| --- | --- | --- |
| ESP32-S3 | Yes (FS) | Two-board S3 peer suite: enumeration, class transfers, ownership, reset/recovery and failure injection |
| ESP32-P4 (v1.3) and P4X (v3.2) | Yes (HS 480 Mbps and FS) | A P4 and a P4X linked at HS run the 39 S3 peer tests and HS-specific probes, including PSRAM-backed storage and rings, in both directions. A one-board FS loopback covers both controllers, all 10 classes, Audio, borrowed DMA and PSRAM. A P4X linked at HS to a Linux PC also covers DFU and MSC |
| ESP32-S2 | Compile only | Hardware is being procured |
| ESP32-S31 | Not supported | Hardware is being procured; building requires Arduino-ESP32 4.0.0-RC1 |

Not yet covered: S2/S31 hardware, Arduino-ESP32 4.x, and PC OS compatibility
including Windows and macOS. Remaining work is tracked in
[TODO](TODO.ja.md) and the current state in the [handoff](docs/V3_HANDOFF.ja.md)
(both in Japanese).

### Running the tests

Run the separate [validation environment](validation/README.ja.md) with the
plugin's standard `--combo all`. It runs each registered, physically wired
combination in turn: the S3 pair, the P4 loopback, the P4/P4X HS link and the
P4X-to-PC link. It uses `--noconftest` and is independent of the archived 2.x
tests. Each result names the tested commit and scope; it does not certify every
OS, control sequence or SoC.

The [example index](examples/README.md) covers basic use, composite devices and
resource changes in one binary. To compile, for example:

```sh
arduino-cli compile --profile esp32s3 examples/CdcConsole
python3 tools/build_check.py esp32s3
python3 tools/verify_usb_backend.py
```

## API documentation

The detailed v3 guides are currently in Japanese:

- [Lifecycle, worker settings and migration](docs/V3_MIGRATION.ja.md)
- [Vendor and borrowed transfers](docs/V3_VENDOR_API.ja.md), [CDC](docs/V3_CDC_API.ja.md)
- [HID](docs/V3_HID_API.ja.md), [MIDI](docs/V3_MIDI_API.ja.md), [MSC](docs/V3_MSC_API.ja.md)
- [Audio](docs/V3_AUDIO_API.ja.md), [Video](docs/V3_VIDEO_API.ja.md)
- [NCM](docs/V3_NCM_API.ja.md), [CCID](docs/V3_CCID_API.ja.md), [DFU](docs/V3_DFU_API.ja.md)
- [CH32RV API alignment notes](docs/V3_CH32RV_API_ALIGNMENT.ja.md)
- [API decisions](docs/V3_API_DECISIONS.ja.md), [development plan](docs/V3_DEVELOPMENT_PLAN.ja.md)

The 2.x examples and tests are archived under [legacy/v2](legacy/v2/README.md).
Other documents without the `V3_` prefix may also describe 2.x. They are historical
reference material, not the current API or v3 test gate.
