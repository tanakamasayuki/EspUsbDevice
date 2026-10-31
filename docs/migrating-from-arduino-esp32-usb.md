# Migrating from the Arduino-ESP32 USB API

> 日本語版: [migrating-from-arduino-esp32-usb.ja.md](migrating-from-arduino-esp32-usb.ja.md)

This guide is for sketches written against the USB device API that ships with
the Arduino-ESP32 core: `USB.begin()`, `USBHIDKeyboard`, `USBHIDMouse`,
`USBCDC`, `USBMSC`, `USBMIDI`, `USBVendor` and the rest. EspUsbDevice v3 is
**deliberately not API-compatible** with that stack. Here the sketch states the
controller, the descriptors, each Function's buffers and the raw reports
explicitly, and every step reports why it failed. This page maps each core API
to its v3 counterpart and lists what has no counterpart on either side.

Coming from EspUsbDevice 2.x instead? See [Migrating from v2](migrating-from-v2.md).

## The model change (read this first)

| | Arduino-ESP32 core | EspUsbDevice v3 |
| --- | --- | --- |
| Objects | one global `USB` plus per-class objects | one `EspUsbDevice` (the **Device**) and one object per **Function**; each Function registers with the Device in its constructor |
| Start | each class's `begin()`, then `USB.begin()` | `configure()` each Function if needed, then one `device.begin(config)`. A Function has no public `begin()`; calling one is a compile error |
| Stop | - | `device.end()` releases everything; `end()` → `configure()` → `begin()` changes settings without rebuilding |
| VID / PID / strings | `USB.VID()`, `USB.productName()`, ... | fields of `EspUsbDeviceConfig`, passed to `device.begin()` |
| Buffer sizes | fixed in the core, or build flags | per Function, with `configure()` at run time. Old `CFG_TUD_*` flags are a compile error |
| Callbacks | `onEvent()` at any time | typed callbacks (`onLineCoding`, `onOutputReport`, ...), registered **before** `device.begin()` |
| Device state | `USB.onEvent(ARDUINO_USB_*_EVENT)` | poll `device.started()`, `device.ready()`, `device.suspended()` |
| Errors | mostly silent | `configure()` returns `esp_err_t`; `begin()` returns `bool` and `device.lastError()` holds the reason |
| Composite | classes stack implicitly | any number of Functions; the endpoint budget is checked in `begin()` before the PHY starts |
| Controller | fixed per chip | `config.controller` selects FS or HS on ESP32-P4 |

### The two stacks are exclusive

EspUsbDevice drives the USB OTG controller with its own driver. The core's USB
device stack drives the same controller. In an EspUsbDevice sketch:

- Do not call `USB.begin()` or create any `USBHID*`, `USBCDC`, `USBMSC`,
  `USBMIDI`, `USBVendor` or `FirmwareMSC` object.
- On ESP32-S2/S3, build with *USB Mode: USB-OTG (TinyUSB)* (`USBMode=default`).
- Leave *USB CDC On Boot*, *USB MSC On Boot* and *USB DFU On Boot* disabled for
  the OTG controller.
- `Serial` is therefore not on the OTG port. It comes out on the UART (or, on
  ESP32-P4, on USB Serial/JTAG if you build with `USBMode=hwcdc`). See
  [Troubleshooting, 4](troubleshooting.md#4-build-and-upload).

### Checklist

1. Remove `#include "USB.h"` and the class headers; `#include <EspUsbDevice.h>`.
2. Create one `EspUsbDevice device;` and pass it to each Function constructor.
   Declare Functions before `device.begin()`.
3. Move VID, PID and strings into an `EspUsbDeviceConfig`.
4. Replace every class `begin()` and `USB.begin()` with one
   `device.begin(config)`. Keep `msc.begin(blockCount, blockSize)`; it only sets
   the geometry.
5. Register every callback before `device.begin()`, and check the returned
   `esp_err_t`.
6. Send only while `device.ready()` is `true`.
7. Check `begin()`'s result and print `esp_err_to_name(device.lastError())`.
8. Change the PID (or serial number) while testing, so Windows does not reuse
   what it remembered about the core stack's device.

## USB object → EspUsbDeviceConfig

| Core | v3 | Note |
| --- | --- | --- |
| `USB.VID(v)` / `USB.PID(p)` | `config.vid` / `config.pid` | default `0x303a` / `0x4000`; fine on a bench, not for a product |
| `USB.manufacturerName()` / `productName()` / `serialNumber()` | `config.manufacturer` / `product` / `serialNumber` | UTF-8, up to 126 UTF-16 units each |
| `USB.firmwareVersion(v)` | `config.deviceVersion` | `bcdDevice`, default `0x0100` |
| `USB.usbPower(mA)` | `config.maxPowerMilliamps` | default 100 |
| `USB.usbAttributes(...)` | `config.selfPowered`, `config.remoteWakeupSupported` | |
| `USB.usbVersion()` | - | `bcdUSB` is 0x0200, or 0x0201 when a BOS descriptor exists |
| `USB.usbClass()` / `usbSubClass()` / `usbProtocol()` | - | set automatically; `0xEF/0x02/0x01` whenever an IAD is present |
| `USB.webUSB(true)` / `USB.webUSBURL(url)` | `config.webusbEnabled` / `config.webusbUrl` | |
| `USB.onEvent(...)` | `device.ready()`, `device.suspended()`, `device.started()` | no device-level event callback |
| `USB.begin()` | `device.begin(config)` | returns `bool`; reason in `device.lastError()` |

```cpp
EspUsbDeviceConfig config;
config.vid = 0x303a;
config.pid = 0x4001;
config.manufacturer = "Example";
config.product = "My Keyboard";
config.serialNumber = "0001";
if (!device.begin(config)) {
  Serial.println(esp_err_to_name(device.lastError()));
}
```

## Keyboard: `USBHIDKeyboard` → `EspUsbDeviceHidKeyboard`

Originally:

```cpp
#include "USB.h"
#include "USBHIDKeyboard.h"
USBHIDKeyboard Keyboard;

void setup() {
  Keyboard.begin();
  USB.begin();
}
void loop() {
  Keyboard.println("Hello");
  Keyboard.press(KEY_LEFT_GUI);
  Keyboard.press('r');
  Keyboard.releaseAll();
}
```

With v3:

```cpp
#include <EspUsbDevice.h>
EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);

void setup() {
  keyboard.setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_US);  // the host's layout
  EspUsbDeviceConfig config;
  config.pid = 0x4001;
  config.product = "My Keyboard";
  device.begin(config);
}
void loop() {
  if (!device.ready()) { delay(10); return; }
  keyboard.write("Hello\n");
  keyboard.tapUsage(ESP_USB_HID_KEY_R, ESP_USB_DEVICE_MOD_LEFT_GUI);
  delay(1000);
}
```

| Core | v3 | Note |
| --- | --- | --- |
| `Keyboard.print()` / `println()` / `write(c)` | `keyboard.write("text")`, `keyboard.tapKey(c)` | layout-aware. `write()` returns `false` at the first character the layout cannot type |
| `Keyboard.begin(layout)` | `keyboard.setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_*)` | match the layout the **host** uses |
| `Keyboard.press(KEY_LEFT_GUI)` then `press('r')` | `keyboard.pressUsage(ESP_USB_HID_KEY_R, ESP_USB_DEVICE_MOD_LEFT_GUI)` or `tapUsage(...)` | HID usage IDs and modifier bits are the primary API |
| `Keyboard.press(c)` / `release(c)` | `keyboard.pressKey(c)` / `releaseKey(c)` | `releaseKey(c)` also releases the modifiers the layout uses for that character; `releaseUsage(usage)` for usage IDs |
| several `press()` calls held together | `pressUsage()` / `pressKey()` | each press adds to the held keys and modifiers, as in the core. Without NKRO six non-modifier keys can be held; a seventh fails with `ESP_ERR_INVALID_SIZE` and changes nothing. `enableNkro()` for more |
| `Keyboard.releaseAll()` | `keyboard.releaseAll()` | also releases modifiers |
| `Keyboard.pressRaw()` / `sendReport(KeyReport*)` | `keyboard.sendReport(EspUsbDeviceBootKeyboardReport)` | byte-exact boot report |
| `onEvent(ARDUINO_USB_HID_KEYBOARD_LED_EVENT)` | `keyboard.onOutputReport(...)` (before `begin()`) or poll `keyboard.ledState()` | |
| - | `keyboard.enableNkro()` + `EspUsbDeviceNkroKeyboardReport` | N-key rollover (usages 0x00-0xDF) |
| - | `keyboard.onProtocol()` / `protocol()` | detect the BIOS/UEFI boot-protocol switch |

## Mouse: `USBHIDMouse` → `EspUsbDeviceHidMouse`

| Core | v3 | Note |
| --- | --- | --- |
| `Mouse.move(x, y, wheel, pan)` | `mouse.move(x, y, wheel, buttons)` | **the 4th argument is the button state, not pan.** Its default `EspUsbDeviceHidMouse::KeepButtons` keeps the buttons held by `press()`, so `press()` then `move()` drags as in the core; 0-255 replaces them |
| `Mouse.click(MOUSE_LEFT)` | `mouse.click(ESP_USB_DEVICE_MOUSE_LEFT)` | `LEFT` / `RIGHT` / `MIDDLE` / `BACK` / `FORWARD` |
| `Mouse.press(b)` / `release(b)` | `mouse.press(b)` / `release(b)` / `releaseAll()` | button masks |
| `Mouse.isPressed(b)` | `(mouse.buttons() & b) != 0` | |
| - | `mouse.wheel(w)`, `mouse.sendReport(EspUsbDeviceBootMouseReport)` | raw boot mouse report |

There is no horizontal pan and no absolute mouse (`USBHIDAbsoluteMouse`). Both
can be built with `EspUsbDeviceHidCustom` and your own report descriptor.

## Other HID classes

| Core | v3 | Note |
| --- | --- | --- |
| `USBHIDConsumerControl` | `EspUsbDeviceHidConsumerControl` | `press(usage)`, `release()`, `click(usage)`, `sendUsage(usage)`. Usage values are the same Consumer page IDs (for example `ESP_USB_DEVICE_CONSUMER_CONTROL_VOLUME_UP` = 0x00E9). Values above 0x3FF fail with `ESP_ERR_INVALID_ARG` |
| `USBHIDSystemControl` | `EspUsbDeviceHidSystemControl` | `press(usage)` takes the Generic Desktop usage IDs 0x81/0x82/0x83 (`ESP_USB_DEVICE_SYSTEM_CONTROL_POWER_OFF`, `STANDBY`, `WAKE_HOST`). **The core's values 1/2/3 are refused**; use the names |
| `USBHIDGamepad` | `EspUsbDeviceHidGamepad` | `send(x, y, z, rz, rx, ry, hat, buttons)` in the same order, `sendReport()`, `releaseAll()`. Hat values `ESP_USB_DEVICE_GAMEPAD_HAT_*` (0 = center, 1-8). No `leftStick()`/`pressButton()` helpers |
| `USBHIDVendor` | `EspUsbDeviceHidVendor(device, reportSize)` | `sendInput(data, length)`; incoming reports through `onOutputReport()` / `onFeatureReport()` (no `read()`/`available()`); `setFeatureReport()` sets what the host reads |
| `USBHID` + a `USBHIDDevice` subclass | `EspUsbDeviceHidCustom(device, descriptor, length, inputReportSize)` | pass your report descriptor bytes; `sendReport(data, length, reportId)`. No subclassing |

All HID Functions on one Device merge into **one HID interface** with report
IDs, so keyboard + mouse + media keys cost a single IN endpoint. In a composite,
report IDs are assigned for you and `sendReport()` adds them.

## Serial: `USBCDC` → `EspUsbDeviceCdcSerial`

Originally:

```cpp
#include "USB.h"
USBCDC USBSerial;

void setup() {
  USBSerial.begin();
  USB.begin();
}
void loop() { USBSerial.println("tick"); delay(1000); }
```

With v3:

```cpp
#include <EspUsbDevice.h>
EspUsbDevice device;
EspUsbDeviceCdcSerial usbSerial(device, "Console");

void setup() {
  EspUsbBulkStreamConfig buffers;      // optional; defaults are 512/512/512
  buffers.rxCapacity = 1024;
  usbSerial.configure(buffers);
  usbSerial.onLineState([](const EspUsbDeviceCdcLineState &s) { /* s.dtr, s.rts */ });
  device.begin();
}
void loop() {
  if (usbSerial.connected()) usbSerial.println("tick");
  delay(1000);
}
```

| Core | v3 | Note |
| --- | --- | --- |
| `USBSerial.begin(baud)` | (remove) | `device.begin()` starts it. The baud rate is whatever the host sets; read it with `lineCoding()` |
| `print` / `println` / `write` / `read` / `available` / `peek` / `readBytes` / `setTimeout` | same | `EspUsbDeviceCdcSerial` is an Arduino `Stream`; pass it as `Stream&` |
| `setRxBufferSize(n)` / TX buffer | `EspUsbBulkStreamConfig::rxCapacity` / `txCapacity` via `configure()` | set while stopped |
| `setTxTimeoutMs(ms)` | `setTxTimeoutMs(ms)` | default 1000 ms. `write()` and `flush()` wait up to this for room or completion; `0` returns a partial count at once |
| `if (USBSerial)` | `usbSerial.connected()` | `true` while the host has the port open with DTR set |
| `onEvent(ARDUINO_USB_CDC_LINE_CODING_EVENT)` | `onLineCoding(callback)` | before `device.begin()` |
| `onEvent(ARDUINO_USB_CDC_LINE_STATE_EVENT)` | `onLineState(callback)` | |
| `onEvent(ARDUINO_USB_CDC_RX_EVENT)` | `onRx(callback)` | |
| `enableReboot()` (restart into the bootloader on the host's request) | `onLineCoding()` + `device.rebootToBootloader()` from `loop()` | v3 never restarts by itself; see [Firmware Update over USB](ota-over-usb.md#24-entering-boot-mode-from-the-sketch) |
| `USB CDC On Boot` (`Serial` on USB) | - | `Serial` stays separate; the CDC Function is a data port of your sketch |

Several CDC ports are several `EspUsbDeviceCdcSerial` objects. Give each a name
so Windows can tell them apart. Each uses two IN endpoints.

## Mass storage: `USBMSC` → `EspUsbDeviceMsc`

The callback shapes stay the same; the calls move before `device.begin()`.

```cpp
EspUsbDevice device;
EspUsbDeviceMsc msc(device);

void setup() {
  msc.vendorID("ESP32");
  msc.productID("Disk");
  msc.productRevision("1.0");
  msc.onRead([](uint32_t lba, uint32_t offset, void *buffer, uint32_t size) -> int32_t { /* ... */ return size; });
  msc.onWrite([](uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size) -> int32_t { /* ... */ return size; });
  msc.begin(blockCount, 512);   // geometry only; needs onRead() first
  msc.mediaPresent(true);
  device.begin();               // starts USB, including MSC
}
```

| Core | v3 | Note |
| --- | --- | --- |
| `vendorID()` / `productID()` / `productRevision()` | same names | return `esp_err_t`; only while stopped |
| `onRead()` / `onWrite()` / `onStartStop()` | same names, same parameters | before `device.begin()`. Callbacks return the whole requested size synchronously |
| `mediaPresent(bool)` | same | media starts absent. Re-inserting while the host is configured reports UNIT ATTENTION once |
| `begin(blockCount, blockSize)` | same name | sets the geometry only; `device.begin()` starts USB |
| `isWritable(bool)` | same | writable by default |
| `end()` | `device.end()` | |
| - | `EspUsbMscConfig::transferBytes` via `configure()` | staging size, default 512. Must be a multiple of the bulk MPS (512 at HS) |

Ready-made backends spare most sketches the callbacks:
[`EspUsbDeviceMscRamDisk`](../examples/MSC/) (raw blocks),
[`EspUsbDeviceMscFatRamDisk`](../examples/MSCFatRamDisk/) (a FAT12 image with
file helpers) and [`EspUsbDeviceMscSdCard`](../examples/MSCSdCard/) (an SD card).
Each attaches with `disk.attach(msc)` before `device.begin()`. RAM disks start
with the medium absent; call `msc.mediaPresent(true)`.

### `FirmwareMSC` / *USB MSC On Boot* → `EspUsbDeviceMscFirmwareDisk`

The core's firmware drive becomes `EspUsbDeviceMscFirmwareDisk`: a FAT volume
whose data region is the spare OTA partition. It accepts a raw `.bin` or a
`.uf2`. See [Firmware Update over USB, 3.6](ota-over-usb.md#36-the-firmware-drive)
and [`FirmwareMSC`](../examples/FirmwareMSC/).

## MIDI: `USBMIDI` → `EspUsbDeviceMidi`

```cpp
EspUsbDeviceMidi midi(device);            // or (device, inCables, outCables)

midi.noteOn(0, 60, 96);                   // channel, note, velocity[, cable]
midi.noteOff(0, 60, 0);
midi.controlChange(0, 74, 32);

EspUsbDeviceMidiPacket packet;
while (midi.readPacket(packet)) { /* 4-byte USB-MIDI event */ }
```

| Core | v3 |
| --- | --- |
| `MIDI.noteOn(note, velocity, channel)` with channel 1-16 | `midi.noteOn(channel, note, velocity, cable)` with **channel 0-15 first** |
| `noteOff`, `controlChange`, `programChange`, `polyPressure`, `channelPressure` | same names, channel first, 0-based |
| `pitchBend(value, channel)` | `pitchBend(channel, value)`, value 0..16383 (8192 = center) |
| `readPacket(midiEventPacket_t*)` / `writePacket()` | `readPacket(EspUsbDeviceMidiPacket&)` / `writePacket()` |

**Watch the argument order.** A mechanical port compiles and then misbehaves:
the core's `noteOn(60, 96, 1)` is read as channel 60, which is out of range, so
the call returns `false` with `ESP_ERR_INVALID_ARG` and sends nothing. Values
are never clamped; arguments that happen to be in range send a wrong note. Also
subtract 1 from the core's 1-based channel. Multi-cable interfaces (up to 16 in
each direction), callback receive (`onPacket()`), and buffer sizes in packets
(`EspUsbMidiConfig`) have no core equivalent; see
[`MIDIInterface`](../examples/MIDIInterface/).

## Vendor: `USBVendor` → `EspUsbDeviceVendor`

| Core | v3 | Note |
| --- | --- | --- |
| `Vendor.write()` / `read()` / `available()` | same names (bulk IN/OUT) | `EspUsbDeviceVendor` is an Arduino `Print`. Buffers via `configure(EspUsbVendorConfig)` |
| `Vendor.onRequest(handler)` | `vendor.onControlRequest(callback)` | before `device.begin()`; the callback returns `bool` |
| `Vendor.sendResponse(rhport, request, data, len)` | `vendor.sendControlResponse(request, data, len)` | |
| `Vendor.onEvent(ARDUINO_USB_VENDOR_DATA_EVENT)` | `onRx(callback)` | |
| `USB.webUSB(true)` / `webUSBURL(url)` | `config.webusbEnabled` / `config.webusbUrl` | |

The library emits a Microsoft OS 2.0 descriptor set for every Vendor interface
automatically, so Windows can bind WinUSB without an INF or Zadig (not checked
with v3 on Windows). `config.webusbEnabled` only adds the WebUSB capability.
A stream write does not end with a short packet by default; read fixed lengths
on the host, or set `streamTermination`. Zero-copy transfers are available with
`writeDirect()` / `readDirect()`. See [`USBVendor`](../examples/USBVendor/).

## DFU: *USB DFU On Boot* → `EspUsbDeviceDfu`

`EspUsbDeviceDfu(device, EspUsbDeviceDfuMode::Runtime)` answers `dfu-util -e`
(`DFU_DETACH`) and restarts into the ROM loader by default.
`EspUsbDeviceDfuMode::Download` (the default) accepts `dfu-util -D firmware.bin`
and writes the spare OTA partition. Both use EP0 only. See
[Firmware Update over USB](ota-over-usb.md).

## What only EspUsbDevice has

- **USB Audio** (UAC1 by default, UAC2 selectable): playback, capture and
  duplex streams.
- **USB Video** (UVC camera, MJPEG or YUY2).
- **CDC-NCM network device** (`EspUsbDeviceNet`) with optional esp_netif/lwIP
  and DHCP.
- **CCID smart card reader** (`EspUsbDeviceCcid`).
- **DFU in the application** and a firmware drive that accepts UF2.
- NKRO keyboard, raw report APIs and boot-protocol observation.
- **ESP32-P4 High Speed** (480 Mbps) with `config.controller`.
- Run-time buffer sizes, `end()` and restart without rebuilding, and
  zero-copy bulk transfers.
- Descriptor inspection without a host (`EspUsbDeviceStartMode::DescriptorsOnly`,
  [`EspUsbDeviceDescriptorDump`](../examples/Info/EspUsbDeviceDescriptorDump/)).

## What is not here

- `USBHIDAbsoluteMouse` and mouse pan: write them with `EspUsbDeviceHidCustom`.
- Gamepad helper methods (`leftStick()`, `pressButton()`, ...): use `send()` or
  `sendReport()`.
- A USB console (`USB CDC On Boot`, `HWCDC` on the OTG port): logs stay on the
  UART or on USB Serial/JTAG.
- Automatic restart into the bootloader from the CDC port: implement it with
  `onLineCoding()` and `rebootToBootloader()`.
- A device-level event callback (`USB.onEvent`): poll `ready()` and
  `suspended()`.

If something misbehaves after porting, see [Troubleshooting](troubleshooting.md),
particularly the
[descriptor cache entry](troubleshooting.md#the-host-still-shows-the-old-device-after-a-change):
a board that keeps the core stack's VID/PID confuses Windows right after a
migration.
