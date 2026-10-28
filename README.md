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
hardware qualification are separate: consult the validation record below.

## Build and validation

The current development baseline is **Arduino-ESP32 3.3.12**. Examples pin that
version in `sketch.yaml`; older-core compatibility reports describe earlier
library versions, not qualification of v3.

| Target | Current v3 evidence |
| --- | --- |
| ESP32-S3 | Two-board FS peer suite: enumeration, class transfers, ownership, reset/recovery and failure injection |
| ESP32-S2 | Controller/resource compile probe; hardware validation pending |
| ESP32-P4 | Controller/resource compile probe including HS; FS/HS hardware validation pending |
| ESP32-S31 | Future SDK/controller port; not currently supported |

Use two separate programming/log connections and connect the native OTG ports
for the S3 peer tests. Run the clean [validation environment](validation/README.ja.md)
with the plugin's standard `--combo all`. It uses `--noconftest` and is independent
of the archived 2.x tests. Results there identify the tested commit and scope;
they do not certify every OS, control sequence or SoC.

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
