# Migrating from the Arduino-ESP32 USB API

> 日本語版: [migrating-from-arduino-esp32-usb.ja.md](migrating-from-arduino-esp32-usb.ja.md)

This guide is for sketches built on the Arduino-ESP32 core's USB device API -
`USB.begin()`, `USBHIDKeyboard`, `USBHIDMouse`, `USBCDC`, `USBMSC`, `USBMIDI`,
`USBVendor`, and friends. EspUsbDevice is **deliberately not API-compatible**
with that stack: here the sketch controls the port, speed, descriptors,
endpoint packet sizes, and raw class reports explicitly. This page maps each
core API to its equivalent, shows the payoff, and lists what has no
counterpart on either side.

## The model change (read this first)

| | Arduino-ESP32 core | EspUsbDevice |
|---|---|---|
| Entry point | per-class `begin()`, then a global `USB.begin()` | one `EspUsbDevice` object; classes register themselves in their constructors; one `device.begin(config)` |
| VID / PID / strings | `USB.VID()` / `USB.PID()` / `USB.productName()` … setters (or build flags) | fields of `EspUsbDeviceConfig`, passed to `begin()` |
| Descriptors | owned by the core | owned by the library and inspectable byte for byte ([DescriptorDump](../examples/Info/EspUsbDeviceDescriptorDump/)) |
| Events | `USB.onEvent()` global event hub | per-class callbacks (`onOutputReport`, `onLineState`, …) plus `device.ready()` |
| Errors | mostly silent | `begin()` returns `false`; `device.lastErrorName()` names the reason |
| Composite | classes stack implicitly | up to 4 classes register explicitly; endpoint budget checked before the PHY starts |

The two stacks are **mutually exclusive**: never call `USB.begin()` in an
EspUsbDevice sketch, build with `USB Mode` = "USB-OTG (TinyUSB)", and leave
`USB CDC On Boot` disabled. `Serial` therefore comes out on the UART - see
[the guide, section 3.5](usb-device-guide.md#35-mutually-exclusive-with-the-stock-arduino-esp32-usb-stack).

### Migration checklist

1. Remove `#include "USB.h"` and every `USB.*` call.
2. Create one `EspUsbDevice device;` and pass it to each class constructor.
3. Move VID/PID/manufacturer/product/serial into an `EspUsbDeviceConfig`.
4. Replace the per-class `begin()` + `USB.begin()` pair with a single
   `device.begin(config)` after all classes are constructed.
5. Gate sending on `device.ready()` - reports sent while unmounted are
   dropped, not queued.
6. Check `begin()`'s return value and print `device.lastErrorName()`.

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

Is now:

```cpp
#include "EspUsbDevice.h"
EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);

void setup() {
  keyboard.setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_US);
  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4001;
  config.product = "My Keyboard";
  device.begin(config);
}
void loop() {
  if (!device.ready()) { delay(10); return; }
  keyboard.write("Hello\n");
  keyboard.tapUsage(ESP_USB_HID_KEY_R, ESP_USB_DEVICE_MOD_LEFT_GUI);
}
```

| Core | EspUsbDevice | Note |
|---|---|---|
| `Keyboard.print()` / `println()` | `keyboard.write("text")` | layout-aware; set the **host's** layout with `setLayout()` |
| `Keyboard.press('a')` / `release()` | `keyboard.pressKey('a')` / `releaseAll()` | ASCII path |
| `Keyboard.press(KEY_LEFT_GUI)` + key | `keyboard.pressUsage(usage, modifiers)` / `tapUsage()` | HID usage IDs and modifier bits are the primary API, not translated key codes |
| `Keyboard.releaseAll()` | `keyboard.releaseAll()` | same idea |
| (no equivalent) | `keyboard.sendReport(...)` | raw boot or NKRO report, byte-exact |
| (no equivalent) | `keyboard.enableNkro()` + `EspUsbDeviceNkroKeyboardReport` | true N-key rollover with a 224-bit bitmap |
| (no equivalent) | `keyboard.ledState()` / `onOutputReport()` | NumLock/CapsLock/ScrollLock from the host |
| (no equivalent) | `keyboard.onProtocol()` | detect the BIOS/UEFI boot-protocol switch |

## Mouse: `USBHIDMouse` → `EspUsbDeviceHidMouse`

| Core | EspUsbDevice | Note |
|---|---|---|
| `Mouse.move(x, y, wheel)` | `mouse.move(x, y, wheel)` | same shape |
| `Mouse.click(MOUSE_LEFT)` | `mouse.click(ESP_USB_DEVICE_MOUSE_LEFT)` | constants: `LEFT` / `RIGHT` / `MIDDLE` / `BACK` / `FORWARD` |
| `Mouse.press()` / `release()` | `mouse.press(buttons)` / `release(buttons)` / `releaseAll()` | button masks |
| (no equivalent) | `mouse.sendReport(...)` | raw boot mouse report |

`USBHIDAbsoluteMouse` has **no counterpart** - this library implements the
relative boot mouse only.

## Media keys and more HID: consumer / system control / gamepad / vendor HID

- `USBHIDConsumerControl` → [`EspUsbDeviceHidConsumerControl`](../examples/MediaKeys/):
  `media.click(ESP_USB_DEVICE_CONSUMER_CONTROL_VOLUME_UP)` etc., or any raw
  16-bit consumer usage via `press()` / `sendUsage()`.
- `USBHIDSystemControl` → `EspUsbDeviceHidSystemControl` (power / sleep / wake).
- `USBHIDGamepad` → [`EspUsbDeviceHidGamepad`](../examples/Gamepad/):
  one `gamepad.send(x, y, z, rz, rx, ry, hat, buttons)` call.
- `USBHIDVendor` → [`EspUsbDeviceHidVendor`](../examples/VendorHID/):
  vendor-defined HID with interrupt IN/OUT and feature reports.
- `USBHID` + a custom `USBHIDDevice` subclass →
  [`EspUsbDeviceHidCustom`](../examples/CustomHID/): pass your own report
  descriptor bytes; no subclassing required.

All HID classes above merge into **one HID interface** with report IDs when
combined, so a keyboard + mouse + media-keys device costs a single IN
endpoint ([guide 3.3](usb-device-guide.md#33-the-endpoint-budget)).

## Serial: `USBCDC` → `EspUsbDeviceCdcSerial`

Originally:

```cpp
#include "USB.h"
USBCDC USBSerial;

void setup() {
  USBSerial.begin();
  USB.begin();
}
void loop() { USBSerial.println("tick"); }
```

Is now:

```cpp
#include "EspUsbDevice.h"
EspUsbDevice device;
EspUsbDeviceCdcSerial UsbSerial(device);

void setup() {
  UsbSerial.onLineState([](const EspUsbDeviceCdcLineState &s) {
    // s.dtr / s.rts
  });
  EspUsbDeviceConfig config;
  device.begin(config);
}
void loop() {
  if (UsbSerial.connected()) UsbSerial.write((const uint8_t *)"tick\n", 5);
}
```

`read()` / `available()` / `write()` / `flush()` carry over as expected. The
core's `onEvent(ARDUINO_USB_CDC_*)` events become the typed callbacks
`onLineCoding()`, `onLineState()`, and `onRx()`. Note that this CDC is a data
port for your sketch - it is not the Arduino console (`Serial` stays on the
UART, [guide 2.3](usb-device-guide.md#23-connector-layout-while-developing)).

## Mass storage: `USBMSC` → `EspUsbDeviceMsc`

The callback trio survives almost unchanged; capacity moves out of `begin()`:

| Core | EspUsbDevice |
|---|---|
| `msc.vendorID("ESP32")` / `productID()` / `productRevision()` | same names |
| `msc.onRead()` / `onWrite()` / `onStartStop()` | same names |
| `msc.mediaPresent(true)` | same name; plus `isWritable(bool)` |
| `msc.begin(blockCount, blockSize)` | capacity comes from the attached backend; registration happens in the constructor |

What is new: ready-made backends so most sketches never write block callbacks
at all - [`EspUsbDeviceMscRamDisk`](../examples/MSC/) (raw blocks),
[`EspUsbDeviceMscFatRamDisk`](../examples/MSCFatRamDisk/) (a real FAT12 image
with file helpers), and [`EspUsbDeviceMscSdCard`](../examples/MSCSdCard/)
(SD card passthrough). Each attaches with `disk.attach(msc)`.

## MIDI: `USBMIDI` → `EspUsbDeviceMidi`

```cpp
EspUsbDeviceMidi MIDI(device);          // or (device, inCables, outCables)

MIDI.noteOn(0, 60, 96);                 // channel, note, velocity[, cable]
MIDI.noteOff(0, 60, 0);
MIDI.controlChange(0, 74, 32);

EspUsbDeviceMidiPacket packet;
while (MIDI.readPacket(packet)) { /* 4-byte USB-MIDI event */ }
```

Watch the argument order: **the channel comes first here** (0-based), and the
optional USB-MIDI **cable** number comes last. The core's helper methods take
the channel as a trailing argument, so a mechanical port will send wrong
notes rather than fail to compile. Multi-cable interfaces (up to
asymmetric in/out counts) and raw 4-byte packet I/O have no core equivalent -
see [`MIDIInterface`](../examples/MIDIInterface/).

## Vendor: `USBVendor` → `EspUsbDeviceVendor`

| Core | EspUsbDevice |
|---|---|
| `Vendor.write()` / `read()` / `available()` | same names (bulk IN/OUT) |
| `Vendor.onEvent()` request handling | `onControlRequest()` + `sendControlResponse()` |
| `USB.webUSB(true)` / `USB.webUSBURL(url)` | `config.webusbEnabled = true` / `config.webusbUrl = "..."` |

With `webusbEnabled` the device serves the BOS and MS OS 2.0 descriptors, so
Windows binds WinUSB automatically - no Zadig, no INF. See
[`USBVendor`](../examples/USBVendor/) for the browser-side pairing.

## What only EspUsbDevice has

- **USB Audio** (UAC1 default, UAC2 selectable): speaker, microphone, and
  duplex headset streams - the core has no audio class at all.
- **CDC-NCM network device** (`EspUsbDeviceNet`): the board becomes a USB
  network interface with lwIP/esp_netif behind it.
- **CCID smart card reader** (`EspUsbDeviceCcid`).
- **NKRO keyboard**, raw report APIs, boot-protocol observation.
- **ESP32-P4 controller selection** (FS or HS) via `config.controller`.
- Byte-level descriptor inspection and an endpoint-budget readout before any
  host is attached.

## What is not here

- `USBHIDAbsoluteMouse` - relative boot mouse only.
- `FirmwareMSC` (flash-update-over-MSC) - planned; tracked in
  [TODO.ja.md](../TODO.ja.md).
- The USB console (`USB CDC On Boot` / `HWCDC`) - by design; logs stay on the
  UART.

If something misbehaves after porting, work through
[troubleshooting.md](troubleshooting.md) - the
[descriptor-cache entry](troubleshooting.md#the-host-still-shows-the-old-device-after-a-change)
in particular, because a re-flashed board keeping the core stack's old VID/PID
will confuse Windows within minutes of a migration.
