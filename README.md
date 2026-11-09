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

New to the library? Start with the [example workflow](docs/example-workflow.md);
to find an example by goal, see the [example list](examples/README.md). When
something does not work, run the bring-up check in [Info](examples/Info/) first.

Compatibility with the Arduino-ESP32 `USB`, `USBHIDKeyboard`, `USBCDC`, `USBMSC`
classes is not a goal. Coming from those, the
[migration guide](docs/migrating-from-arduino-esp32-usb.md) maps one to the other.

## Supported chips and classes

| Chip | Speed | [HID](docs/classes/hid.md) | [CDC](docs/classes/cdc.md) | [Vendor](docs/classes/vendor.md) | [MIDI](docs/classes/midi.md) | [MSC](docs/classes/msc.md) | [Audio](docs/classes/audio.md) | [Video](docs/classes/video.md) | [NCM](docs/classes/ncm.md) | [CCID](docs/classes/ccid.md) | [DFU](docs/classes/dfu.md) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| ESP32-S3 | FS (12 Mbps) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| ESP32-P4 v1.x and P4X (v3.1+) | HS (480 Mbps) and FS | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| ESP32-S2 | FS (12 Mbps) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

✅ = transfers checked on hardware by the `tests/` probes: two ESP32-S3
boards at full speed; an ESP32-S2 and an S3 linked at full speed in both
directions; a P4 and a P4X linked at high speed in both directions and one P4
linking its own HS and FS controllers; a P4X and an S2 each against a Linux PC.
ESP32-S31 is not supported yet.

Classes:

- **HID:** keyboard (6-key or NKRO, layout conversion, LED state), mouse,
  gamepad, Consumer Control (media keys), System Control, vendor-defined
  reports, and a report descriptor written by the sketch
- **CDC ACM:** USB serial ports; an Arduino `Stream`, several per device
- **Vendor:** bulk transfers and vendor control requests, with generated WebUSB
  and Microsoft OS 2.0 (WinUSB) descriptors
- **MIDI 1.0:** up to 16 cables per direction
- **MSC:** RAM, FAT12 RAM, SD card and firmware disks through helpers
- **Audio:** UAC1 and UAC2 playback and capture
- **Video:** a UVC camera (MJPEG or YUY2, isochronous or bulk)
- **NCM:** a CDC-NCM USB network, with optional lwIP and DHCP
- **CCID:** a smart card reader whose card is written in the sketch
- **DFU 1.1:** firmware update with `dfu-util`, and runtime DETACH

They combine freely into one composite device; the only limits are the
controller's endpoints and FIFO, and a configuration that does not fit fails
`begin()`.

## Requirements

- Arduino-ESP32 **3.3.12** (the examples pin it in `sketch.yaml`).
- ESP32-S3 and ESP32-S2: set **Tools > USB Mode** to **USB-OTG (TinyUSB)**
  (`USBMode=default`); the ESP32-S3 default is Hardware CDC and JTAG.
- ESP32-P4: choose the chip revision in **Tools > Chip Variant**: v1.x is
  `prev3`, v3.1 or later (P4X) is `postv3`.
- Do not enable the core's own USB CDC, MSC or DFU on boot, and do not call
  `USB.begin()`: this library owns the USB OTG controller.

## What changed in 3.0

3.0 is a major version and 2.x sketches need changes; the steps are in
[Migrating from v2](docs/migrating-from-v2.md).

### From TinyUSB to the library's own USB stack

2.x built a bundled TinyUSB with the library's own `tusb_config.h`. Class
buffer sizes were `CFG_TUD_*` build flags: changing one meant writing a
`build_opt.h` and rebuilding everything, and buffers for unused classes were
still reserved at link time.

3.0 uses neither TinyUSB's classes nor its common stack. All USB handling is
the library's own code.

```text
Sketch
  ↓
EspUsbDevice and Functions (HID, CDC, Vendor, MIDI, MSC, Audio, Video, NCM, CCID, DFU)
  ↓
Descriptor building, endpoint allocation, transfer contracts
  ↓
Core (EP0, standard requests, event queue) and runtime (PHY, controller choice, USB worker task)
  ↓
DWC2 driver (registers, FIFO, DMA, interrupts, cache)
  ↓
ESP-IDF (PHY, FreeRTOS, heap)
```

- The class drivers and the common core are new code written to USB 2.0 and
  the class specifications.
- Only the DWC2 controller driver derives from TinyUSB (MIT) and ST
  (BSD-3-Clause) code, maintained as part of the library; its origin and the
  scope of changes are in [third_party/tinyusb](third_party/tinyusb/PROVENANCE.md).
- `#include <tusb.h>`, `tud_*()` calls and `CFG_TUD_*` flags are gone. Defining
  an old flag is a compile error that names the replacement API.
- Arduino-ESP32 remains the platform for the SoC, PHY and FreeRTOS. The core's
  own USB Device implementation (`USB.h` and friends) cannot run alongside.

### How usage changed

| Topic | 2.x | 3.0 |
| --- | --- | --- |
| Buffer sizes | `CFG_TUD_*` in `build_opt.h`; rebuild everything | Each Function's `configure()` at run time; may differ per instance |
| Memory | Reserved statically at link time, unused classes included | `begin()` allocates for the Functions in use, `end()` frees |
| Changing a size | Rebuild | `end()` → `configure()` → `begin()` |
| USB task | Call `device.task()` from `loop()` | The library's worker task runs; `device.task()` is gone |
| Starting a Function | `keyboard.begin()` and friends could be called | `device.begin()` only; Function `begin()` is not public |
| Registering callbacks | Any time | Before `device.begin()` only; registration returns `esp_err_t` |
| State | `ready()` | `started()`, `ready()`, `suspended()`, `speed()` |
| Learning about failure | An enumeration failure on the host | `begin()` checks before touching USB; `lastErrorName()` and each Function's `startError()` name the reason |
| Number of Functions | Up to 6 | No fixed limit; the controller's endpoints and FIFO decide |
| ESP32-P4 | Mind the FS and HS port numbers | Choose with `config.controller`; `Auto` is HS; revisions v1.x and P4X |
| Large transfers | Up to 65535 bytes | Borrowed DMA transfers (`writeDirect` / `readDirect`); completion reported exactly once |
| Where rings live | Fixed internal RAM | The sketch can supply them, PSRAM included |

Measured on ESP32-S3 with examples of the same function, static RAM went from
about 59.5 KB to about 22 KB and flash shrank by 1.6-10 %. 3.0 allocates its
buffers from the heap in `begin()`, so check the free heap after `begin()` too.

### Changes that still compile

These compile unchanged and only behave differently; most are fixes toward the
specifications.

- A callback registered after `device.begin()` is never called (registration
  returns `ESP_ERR_INVALID_STATE`).
- Keyboard `pressUsage()` adds to the keys held (up to six); `tapKey()`
  releases only what it added.
- `mouse.move()` keeps held buttons.
- System Control takes the standard usages 0x81-0x83; out-of-range MIDI or
  Consumer values are refused rather than clamped.
- CDC `write()` / `flush()` wait up to 1000 ms to send (`setTxTimeoutMs()`).
- Re-inserting MSC media reports UNIT ATTENTION once on the next command; the
  default staging is 512 bytes.
- UAC2 full-speed feedback is 3 bytes (10.14); a WebUSB URL without a scheme is
  sent as written.

The full list is in
[Migrating from v2, "Changes the compiler will not catch"](docs/migrating-from-v2.md).

## The basic shape

```cpp
#include <EspUsbDevice.h>

EspUsbDevice device;                       // declare the Device first
EspUsbDeviceVendor vendor(device);         // a Function registers itself with it

void setup() {
  Serial.begin(115200);

  EspUsbVendorConfig buffers = vendor.configuration();   // start from the current settings
  buffers.rxCapacity = 4096;
  if (vendor.configure(buffers) != ESP_OK) return;       // succeeds only while stopped

  vendor.onRx([](size_t) { /* runs on the USB worker; keep it short */ });   // before begin

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4002;
  config.product = "My device";
  if (!device.begin(config)) {
    Serial.printf("begin: %s (vendor: %s)\n", device.lastErrorName(),
                  esp_err_to_name(vendor.startError()));
  }
}

void loop() {
  if (!device.ready() || device.suspended()) return;   // wait until the host configures it
  uint8_t data[64];
  const size_t n = vendor.read(data, sizeof(data));
  if (n) vendor.write(data, n);                        // returns the bytes accepted
}
```

- **Order:** declare → `configure()` → register callbacks → `device.begin()`.
  To change anything, `device.end()` first.
- **State:** `started()` is true from a successful `begin()` until `end()`,
  `ready()` once the host has configured the device (SET_CONFIGURATION), and
  `suspended()` while the bus is suspended. `speed()` returns the negotiated speed.
- **Callbacks** all run on the USB worker task. Do not wait, print at length or
  call `begin()` / `end()` there: set a flag or copy the data and do the work
  in `loop()`. The worker's stack is `config.taskStackBytes` (4096 by default).
- **Errors:** `begin()` returns `bool`; `device.lastError()` /
  `lastErrorName()` give the reason. `ESP_ERR_INVALID_SIZE` means the
  configuration does not fit the endpoints or FIFO, `ESP_ERR_INVALID_ARG` a
  wrong setting, `ESP_ERR_INVALID_STATE` a call at the wrong time.
  [Troubleshooting](docs/troubleshooting.md) is indexed by symptom.
- **Checking ahead:** with `config.startMode =
  EspUsbDeviceStartMode::DescriptorsOnly` the descriptors are built and the
  configuration checked without starting USB; DescriptorDump in
  [Info](examples/Info/) uses it.
- **Remote wakeup:** advertise it with `config.remoteWakeupSupported = true`;
  if the host enables it, `device.remoteWakeup()` wakes the host.

## The classes in brief

Every setting, the behaviour and host OS notes for each class are in the
[class guides](docs/classes/).

### HID

```cpp
EspUsbDeviceHidKeyboard keyboard(device);
EspUsbDeviceHidMouse mouse(device);
// in loop()
keyboard.write("hello\n");   // type a string (choose the layout with setLayout())
keyboard.tapUsage(0x28);     // send by usage ID (Enter)
mouse.move(10, 0);
```

- Character helpers (`write`, `tapKey`, `pressKey`) sit beside usage-ID and raw
  report APIs.
- `keyboard.enableNkro(true)` (before `begin()`) turns on N-key rollover. While
  the host has selected the boot protocol (a BIOS, for example) reports go out
  in the six-key format.
- `keyboard.ledState()` returns, by value, the LED state the host sent (Num /
  Caps / Scroll Lock and so on).
- Gamepad, Consumer Control, System Control, vendor-defined reports
  (`EspUsbDeviceHidVendor`) and your own report descriptor
  (`EspUsbDeviceHidCustom`) sit side by side in one device. At high speed on
  the ESP32-P4, interrupt packets can be up to 1024 bytes.

### CDC (USB serial)

- `EspUsbDeviceCdcSerial` is an Arduino `Stream`: `print()`, `readBytes()`,
  `setTimeout()` all work.
- Several can be registered, each with a name
  (`EspUsbDeviceCdcSerial(device, "Console")`); each port uses two IN endpoints.
- Line coding, DTR / RTS and break arrive through callbacks and queries.

### Vendor, WebUSB, WinUSB

- Bulk data moves by copy (`read` / `write`), by callback (`onRxData`) or by
  borrowed DMA (`writeDirect` / `readDirect`).
- Borrowed DMA hands the sketch's buffer to DMA without copying. Completion
  comes back exactly once as an `EspUsbTransferResult`, and the buffer always
  returns, on cancel, bus reset and `end()` too.
- The sketch can answer vendor control requests. The library generates the
  WebUSB landing page and the Microsoft OS 2.0 descriptors that let Windows
  bind a driver automatically, matched to the interfaces actually allocated.

### MIDI

```cpp
EspUsbDeviceMidi midi(device);              // one cable
midi.noteOn(0, 60, 100);                     // channel 0-15, note, velocity
EspUsbDeviceMidiPacket packet;
while (midi.readPacket(packet)) { /* received */ }
```

- `EspUsbDeviceMidi(device, inCables, outCables)` shows the host up to 16
  cables per direction.

### MSC (USB drive)

```cpp
EspUsbDeviceMsc msc(device);
uint8_t storage[32 * 512];
EspUsbDeviceMscRamDisk volume(storage, 32);
// in setup()
volume.attach(msc);
msc.mediaPresent(true);
```

- The library handles BOT and SCSI; block reads and writes go to callbacks or
  a helper.
- Helpers: `EspUsbDeviceMscRamDisk` (raw blocks), `EspUsbDeviceMscFatRamDisk`
  (FAT12 with files you place; the cluster size follows the capacity),
  `EspUsbDeviceMscSdCard` (an SD card), `EspUsbDeviceMscFirmwareDisk` (writing
  a file updates the firmware).
- Storage can live in PSRAM. Media removal and change are reported to the host
  correctly.

### Audio

```cpp
EspUsbAudioFunction audio(device, EspUsbAudioProtocol::Uac2);   // UAC1 when omitted
EspUsbAudioPlaybackStream &playback = audio.addPlaybackStream();
// in setup()
playback.addFormat({48000, 2, 2, 16});   // rate, channels, bytes/sample, bits/sample
// in loop()
size_t n = playback.read(pcm, sizeof(pcm));
```

- One Function can hold Playback (PC → ESP32) and Capture (ESP32 → PC).
- Volume, mute and rate changes are reported through `pollEvent()` and never
  applied to the PCM. I2S and codecs are the application's job.

### Video

- `EspUsbDeviceVideo` is a UVC camera. Hand it MJPEG or YUY2 frames with
  `submitFrame()`; it does not encode JPEG.
- Isochronous or bulk transport is chosen at run time. High speed on the
  ESP32-P4 gives far more bandwidth.

### NCM (USB networking)

- `EspUsbDeviceNet` appears to the PC as a USB network adapter. Recent Windows,
  macOS and Linux need no driver installation.
- `beginNetwork()` creates an lwIP netif, and `dhcpServer(true)` hands the PC
  an address: the shape of a web settings page or API reachable over USB.
- Without IP, raw Ethernet frames can be handled directly (`onFrame()` /
  `sendFrame()`).

### CCID (smart card reader)

- A one-slot, T=1, short-APDU reader. The sketch writes the card's ATR and its
  APDU replies (`insertCard()`, `onApdu()`).
- PC/SC applications on the PC see a reader with a card inserted.

### DFU and firmware update

- `EspUsbDeviceDfu` in Download mode takes `dfu-util -D firmware.bin`, writes
  the spare OTA partition, verifies it and restarts. It uses no endpoints, so
  it fits any configuration. Runtime mode answers only `dfu-util -e` (DETACH).
- `EspUsbDeviceFirmwareUpdate` is a transport-independent OTA writer, usable
  from CDC, Vendor or HTTP over NCM.
- `device.rebootToBootloader()` enters the ROM download loader from a running
  sketch.
- Boot modes per chip and a comparison of the routes are in
  [OTA over USB](docs/ota-over-usb.md).

## Composite devices and endpoints

Declare as many Functions as you need and call `device.begin()` once: the
library numbers the interfaces, allocates endpoints and builds the composite
descriptors. The limit is the controller's resources.

| Controller | Non-control IN endpoints | Endpoint numbers |
| --- | --- | --- |
| ESP32-S2 / S3 | 4 | 1-6 |
| ESP32-P4 FS controller | 4 | 1-6 |
| ESP32-P4 HS controller | 7 | 1-15; bulk up to 512 bytes, interrupt up to 1024 |

- IN endpoints used: CDC and NCM 2, CCID 2 (1 when the card is fixed), Vendor,
  MIDI, MSC and Video 1 each, all HID Functions together 1, DFU 0. Audio
  capture uses 1, and UAC2 playback 1 for its feedback. On an S3, for example,
  HID + CDC + MSC (1 + 2 + 1) uses all four.
- `begin()` checks the endpoint count and the TX FIFO before touching USB and
  returns `ESP_ERR_INVALID_SIZE` if they do not fit. DescriptorDump in
  [Info](examples/Info/) shows the descriptors and the budget without a host.
- Example: [CompositeHidCdcMsc](examples/CompositeHidCdcMsc/) (keyboard +
  serial + FAT disk).

## ESP32-P4 and high speed

- The P4 has an HS controller (480 Mbps, UTMI PHY) and an FS controller.
  `config.controller = Auto` picks HS.
- At HS bulk packets are 512 bytes: MSC `transferBytes` and `readDirect()`
  capacities must be multiples of 512. A configuration that worked at FS can
  fail `begin()` at HS.
- `device.speed()` gives the negotiated speed. The HS controller runs at FS
  behind an FS host or hub.
- At HS the device answers USB 2.0 TEST_MODE.
- Examples: [P4HighSpeedDevice](examples/P4HighSpeedDevice/),
  [P4FullSpeedDevice](examples/P4FullSpeedDevice/).

## Memory and PSRAM

- Buffers are allocated from the heap in `begin()` and freed in `end()`; a
  failure leaves nothing behind.
- Rings and MSC storage that the CPU accesses can be supplied by the sketch,
  in PSRAM if you like.
- What the USB DMA touches (staging, borrowed transfer buffers) is internal
  RAM. A PSRAM buffer passed to a borrowed transfer is refused with
  `NotDmaCapable`.
- See the DMA and PSRAM chapter of the [advanced guide](docs/usb-device-advanced.md).

## Limitations

- Cannot run alongside the Arduino-ESP32 `USB.begin()`, `USBHIDKeyboard`,
  `USBCDC` and friends.
- Networking is CDC-NCM only (no CDC-ECM or RNDIS). For the ESP32 to reach the
  internet through the PC, the PC must bridge or share its connection.
- CCID is one slot, T=1, short APDUs. Extended APDUs, chaining, PIN pads and
  T=0 are not handled.
- Video offers one format, resolution and frame rate per Function, and declares
  no still image or camera controls.
- MSC exposes LUN 0 only per Function. While an SD card is exposed, do not use
  the same card through the ESP32's file API.
- Audio never applies volume or mute to the PCM, and does not follow an
  external codec's clock drift (absorb it in the application by watching the
  ring level).
- Calls from an ISR are not supported.

## Documentation

| Read | For |
| --- | --- |
| [USB device guide](docs/usb-device-guide.md) | USB basics, power and connectors, ESP32 specifics, bring-up |
| [Class guides](docs/classes/) | One per class: usage, every setting, behaviour, host OS notes |
| [Advanced guide](docs/usb-device-advanced.md) | Task model, descriptors byte by byte, control transfers, FIFO, DMA and PSRAM, your own Function |
| [Examples](examples/README.md) | 40+ complete sketches, every 2.x example included |
| [Troubleshooting](docs/troubleshooting.md) | `begin()` errors, enumeration problems, per-OS and per-class issues |
| [OTA over USB](docs/ota-over-usb.md) | Firmware update routes: ROM loader, DFU, drive, serial, vendor, browser |
| [Migrating from v2](docs/migrating-from-v2.md) | Moving an EspUsbDevice 2.x sketch to 3.0 |
| [Migrating from Arduino-ESP32 USB](docs/migrating-from-arduino-esp32-usb.md) | Moving from `USBHIDKeyboard`, `USBCDC`, `USBMSC` and friends |
| [Tests](tests/README.md) | How the hardware tests are organised, and what they cover ([TEST_PLAN](tests/TEST_PLAN.md)) |
| [Changelog](CHANGELOG.md) | What changed in each version |

## Not yet verified

Windows and macOS hosts, real SD cards and OTA writes on hardware, the ESP32-S2
entering its ROM DFU, and Arduino-ESP32 4.x. The library is tested against
ESP32 hosts and a Linux PC; see the [TEST_PLAN](tests/TEST_PLAN.md).

## License

MIT; see [LICENSE](LICENSE). The DWC2 controller driver is derived from TinyUSB
(MIT) and ST code (BSD-3-Clause); see [third_party/tinyusb](third_party/tinyusb/PROVENANCE.md).
