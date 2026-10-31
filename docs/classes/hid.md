# HID (Human Interface Device)

[日本語](hid.ja.md)

## Overview and when to use it

HID is the USB class for input devices such as keyboards, mice and gamepads.
Windows, Linux and macOS all ship a standard driver for it, so nothing has to be
installed on the Host.

The library provides these HID Functions.

| Class | Purpose | Report |
| --- | --- | --- |
| `EspUsbDeviceHidKeyboard` | Keyboard. 6-key rollover (default) or NKRO | boot 8 bytes / NKRO 29 bytes |
| `EspUsbDeviceHidMouse` | Mouse (5 buttons, X/Y, wheel) | report 4 bytes / boot 3 bytes |
| `EspUsbDeviceHidGamepad` | Gamepad (6 axes, hat, 32 buttons) | 11 bytes + ID |
| `EspUsbDeviceHidConsumerControl` | Media keys such as volume and play | 2 bytes + ID |
| `EspUsbDeviceHidSystemControl` | Power off, standby, wake | 1 byte + ID |
| `EspUsbDeviceHidVendor` | Your own data (vendor-defined usage) | `reportSize` bytes + ID |
| `EspUsbDeviceHidCustom` | A report descriptor you write yourself | set by the descriptor |

Which one to use:

- To send key presses or pointer movement: Keyboard, Mouse, Gamepad, Consumer/System Control.
- To exchange your own data without a driver: `EspUsbDeviceHidVendor`. It is slower than bulk, but needs no driver installation, even on Windows.
- When you need speed: use [Vendor bulk](vendor.md) instead of HID.

When two or more HID Functions are registered, they are merged into one HID
interface with one report descriptor and told apart by report ID
([Composite HID](#composite-hid)).

Operation has been checked over a full-speed link between two ESP32-S3 boards
and over a high-speed link between ESP32-P4/P4X boards (both with EspUsbHost as
the Host). Interoperation with PC operating systems has not been checked.

## Basic usage

```cpp
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);
EspUsbDeviceHidMouse mouse(device);

void setup() {
  keyboard.onOutputReport([](const EspUsbDeviceHidKeyboardOutputReport &leds) {
    // Runs on the USB worker. Do not block or call end() here.
  });
  if (!device.begin()) {
    // device.lastError() / device.lastErrorName() give the reason
  }
}

void loop() {
  if (device.ready()) {
    keyboard.write("hello\n");   // send a string as key presses
    mouse.move(10, 0);           // 10 to the right
  }
  delay(1000);
}
```

- Create the Device first, then the Functions, then call `device.begin()`. The Device starts each Function. A Function's own `begin()` is not public, so writing `keyboard.begin()` is a compile error.
- Register callbacks and change settings before `device.begin()`.
- `true` from a send function means "the transfer was started". It does not mean the Host received it.

## Settings

HID has no configuration struct. Settings come from constructor arguments and
from setters called while stopped.

| Item | Default | Range / meaning | FS/HS difference |
| --- | --- | --- | --- |
| `EspUsbDeviceHidVendor(device, reportSize)` | 63 | Payload bytes per report (excluding the report ID). 1 to `maxReportSize()` (65534) | Interrupt packet size: at FS the smaller of `reportSize + 1` and 64 (at least 8). At HS, if `reportSize + 1` exceeds 64, that value (up to 1024) |
| `EspUsbDeviceHidCustom(device, desc, len, inputReportSize)` | 64 | `desc` is the report descriptor. `inputReportSize` is the interrupt IN `wMaxPacketSize` to declare, report ID byte included. A longer report is still sent, over several packets. 0 makes begin fail with `ESP_ERR_INVALID_ARG` | FS: up to 64. HS: a value over 64 is used as is, up to 1024 (table below) |
| `keyboard.enableNkro(true)` | off | NKRO (bitmap of usages 0x00-0xDF, 29 bytes). Only while stopped | none |
| `keyboard.setLayout(layout)` | `ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_US` | Character-to-key table used by `write()`/`tapKey()`. Can be changed while running | none |
| `timeoutMs` of send functions | 100 | How long to wait (ms) while the IN endpoint is busy. 0 does not wait | none |

### Endpoints and packet sizes

| Configuration | Interrupt IN | Interrupt OUT | Packet size (FS) | Packet size (HS) | bInterval |
| --- | --- | --- | --- | --- | --- |
| Keyboard alone | yes | yes (LEDs) | 8 (32 with NKRO) | same | 1 (FS 1 ms, HS 125 µs) |
| Mouse, Gamepad, Consumer, System alone | yes | no | 8 | 8 | same |
| Custom alone | yes | no | `inputReportSize` (8-64) | `inputReportSize` if over 64 (up to 1024); otherwise same as FS | same |
| Vendor alone | yes | yes | `reportSize + 1` (8-64) | `reportSize + 1` (up to 1024) | same |
| Composite HID (two or more HID Functions) | one, shared | one, shared | largest of 16, 32 (NKRO Keyboard), Vendor's `reportSize + 1` and Custom's `inputReportSize` (each capped at 64) | largest of the FS value and the HS value of each Vendor/Custom (up to 1024) | same |

- A report longer than the packet size is sent as several packets in one transfer (USB 2.0 §5.7.3). For example, a Gamepad alone (12 bytes including the ID) goes out as an 8-byte and a 4-byte packet.
- A Keyboard or Mouse registered alone is declared as a boot interface (subclass 1). A composite HID uses report IDs and is therefore not a boot interface ([Standards and design rationale](#standards-and-design-rationale)).
- Non-control IN endpoints are limited, across all Functions, to 4 on ESP32-S3 and 7 on the P4 high-speed controller. HID uses one IN endpoint however many HID Functions you register.
- `inputPacketSize()` on any HID Function returns the `wMaxPacketSize` of the interrupt IN endpoint at the speed the Host negotiated (0 before the bus reset and after end).
- At HS, interrupt packets over 512 bytes may not fit the Host's periodic FIFO. With EspUsbHost on ESP32-P4 as the Host, set `ESP_USB_HOST_FIFO_LARGE_PERIODIC_OUT` on the Host side.

## API

### Rules common to all send functions

- Send functions (`sendReport`, `sendInput`, `tapKey`, `move` and so on) return `bool`. `true` means the data was copied to an internal buffer and the transfer was started.
- While the IN endpoint is busy, they wait up to `timeoutMs`.
- `device.lastError()` tells why a send failed.

| `device.lastError()` | Cause |
| --- | --- |
| `ESP_ERR_INVALID_STATE` | Not connected, not configured by the Host, bus suspended, stopping, or called with a `timeoutMs` from the USB worker (a callback) |
| `ESP_ERR_INVALID_SIZE` | Longer than the declared report, or the wrong length for boot protocol |
| `ESP_ERR_TIMEOUT` | The IN endpoint did not become free within `timeoutMs` |
| `ESP_ERR_INVALID_ARG` | A value the report cannot carry (see each Function below). Nothing is sent |

Calling context:

- From an ordinary task, call with a `timeoutMs`.
- From a callback running on the USB worker, use `timeoutMs = 0`. A call with a wait is rejected.
- Do not call from an interrupt (ISR).

An explicit send is always sent, even if the content is unchanged. Nothing is thinned out.

### Keyboard

| Function | Description |
| --- | --- |
| `write(text, interKeyDelayMs = 5)` | Presses and releases an ASCII string one character at a time. false on a character it cannot map |
| `tapKey(ch, holdMs = 10)` / `pressKey(ch, timeoutMs = 100)` / `releaseKey(ch, timeoutMs = 100)` | Press and release / press / release one character. `releaseKey` also releases the modifiers the layout uses for that character |
| `tapUsage(usage, modifiers = 0, holdMs = 10)` / `pressUsage(usage, modifiers = 0, timeoutMs = 100)` / `releaseUsage(usage, timeoutMs = 100)` | Operate by HID usage ID (`ESP_USB_HID_KEY_*`) |
| `releaseAll()` | Release everything |
| `sendReport(EspUsbDeviceBootKeyboardReport)` | Send the 6-key boot format (8 bytes) |
| `sendReport(EspUsbDeviceNkroKeyboardReport)` | Send the state of every key. false unless `enableNkro()` was called before begin |
| `ledState()` | LED state the Host sent last (Num/Caps/Scroll Lock, Compose, Kana). Updated with or without a callback. Cleared on bus reset or unplug |
| `onOutputReport(cb)` / `onProtocol(cb)` / `enableNkro()` | `ESP_OK` only while stopped. While running they return `ESP_ERR_INVALID_STATE` and change nothing |
| `protocol()` | Current protocol (0 = boot, 1 = report) |
| `heldState()` | The state last sent to the Host with NKRO. Empty while NKRO is off |

- `setLayout()` only decides which key position each character is sent as. The character that appears on screen is decided by the Host's keyboard layout setting. Make the two match.
- `press*` adds to the held state and `release*` removes from it, so keys combine: `pressUsage(A)` then `pressUsage(B)` holds both. Modifiers given in `modifiers` are added to those already held.
- A modifier usage (0xE0-0xE7, Left Control to Right GUI) sets its modifier bit instead of taking a key slot.
- Without NKRO, up to 6 non-modifier keys can be held (the boot report is an array of six, HID 1.11 Appendix B.1). A seventh fails with `ESP_ERR_INVALID_SIZE` and changes nothing. Releasing a key removes only that key and packs the rest to the front.
- `tap*` releases only what it added. A key or modifier already held before the tap stays held.
- `pressKey`/`tapKey`/`releaseKey`/`write` fail with `ESP_ERR_NOT_SUPPORTED` for a character the layout cannot type. With NKRO, a usage the bitmap cannot carry (above 0xDF and not a modifier) fails with `ESP_ERR_INVALID_ARG`.
- With NKRO enabled, `pressKey`/`pressUsage` also go through the NKRO state.
- Even with NKRO enabled, the report is folded down to the 6-key boot format while the Host has selected boot protocol (BIOS/UEFI and the like).

### Mouse

| Function | Description |
| --- | --- |
| `move(x, y, wheel = 0, buttons = KeepButtons, timeoutMs = 100)` | Relative movement. The default `KeepButtons` (-1) keeps the buttons held by `press()`, so `press()` then `move()` drags. 0-255 sets the held buttons, as `press()`/`release()` would. Any other value fails with `ESP_ERR_INVALID_ARG` |
| `wheel(w)` | Wheel only. Keeps the button state |
| `press(b)` / `release(b)` / `click(b, holdMs = 10)` / `releaseAll()` | Buttons (`ESP_USB_DEVICE_MOUSE_LEFT`/`RIGHT`/`MIDDLE`/`BACK`/`FORWARD`) |
| `sendReport(EspUsbDeviceBootMouseReport)` / `sendState()` | Send a report as given / send the current button state |

Report protocol sends 4 bytes (buttons, X, Y, wheel); boot protocol converts it to 3 bytes.

### Gamepad

Send with `send(x, y, z, rz, rx, ry, hat, buttons)` or
`sendReport(EspUsbDeviceGamepadReport)`. Axes are -127 to 127; there are 32
buttons (bit 0 is button 1).

The hat (direction pad) takes `ESP_USB_DEVICE_GAMEPAD_HAT_CENTER` (0 = not
pressed) or a constant from `HAT_UP` (1) clockwise to `HAT_UP_LEFT` (8). Values
1-8 map to 0-315 degrees.

### Consumer Control

Pass a Consumer page usage ID to `press(usage)` / `release()` / `click(usage)` /
`sendUsage(usage)`. The descriptor declares the range 0-0x3FF (Logical Maximum
1023). A larger value fails with `ESP_ERR_INVALID_ARG` and is not sent. Constants such as
`ESP_USB_DEVICE_CONSUMER_CONTROL_VOLUME_UP`, `MUTE`, `PLAY_PAUSE` and
`NEXT_TRACK` are provided.

### System Control

`press` / `click` / `sendUsage` take a standard Generic Desktop page usage ID.

| Constant | Value |
| --- | --- |
| `ESP_USB_DEVICE_SYSTEM_CONTROL_POWER_OFF` | 0x81 |
| `ESP_USB_DEVICE_SYSTEM_CONTROL_STANDBY` | 0x82 |
| `ESP_USB_DEVICE_SYSTEM_CONTROL_WAKE_HOST` | 0x83 |
| (release) | 0 |

Any other value returns false with `ESP_ERR_INVALID_ARG`, sends nothing and leaves the state unchanged.
`WAKE_HOST` is an ordinary HID input. It is not remote wakeup of a suspended
Host (that is `device.remoteWakeup()`).

### Vendor (raw data)

| Function | Return value / constraints |
| --- | --- |
| `sendInput(data, length, timeoutMs = 100)` | true once the transfer starts. false if `length` is 0 or exceeds `reportSize()` (nothing is truncated). A shorter payload is sent as is |
| `onOutputReport(cb)` / `onFeatureReport(cb)` | `ESP_OK` only while stopped; `ESP_ERR_INVALID_STATE` while running |
| `reportSize()` | The payload size given to the constructor |
| `maxReportSize()` | 65534 (the internal transfer length is 16-bit, minus the 1-byte report ID) |

- `maxReportSize()` does not promise the allocation will succeed. Available memory and Host support are limits too.
- begin fails with `ESP_ERR_INVALID_SIZE` if `reportSize` exceeds the limit, and with `ESP_ERR_INVALID_ARG` if it is 0.
- If the receiver expects a fixed length, pad the data in your application.
- Even when used alone, reports carry report ID 6. On the Host side the first byte of a read is the report ID.

### Custom (your own report descriptor)

```cpp
static const uint8_t kReport[] = { /* report descriptor */ };
EspUsbDeviceHidCustom custom(device, kReport, sizeof(kReport), 64);
// after begin
custom.sendReport(data, length);       // reportId 0 = unnumbered alone, the assigned ID in a composite
```

- Keep `kReport` valid and unchanged while running. It is not copied.
- At begin the descriptor is parsed. Report Size / Count / ID and global Push/Pop give the length of each ID and type (Input/Output/Feature).
- begin rejects: truncated items, unbalanced Push/Pop, an explicit Report ID 0, a mix of numbered and unnumbered reports, and lengths over the transfer limit.
- This does not check every usage or its meaning. Verify the descriptor yourself (see [Host OS side](#host-os-side) for how).

### Feature report values

```cpp
uint8_t value[] = {1, 2, 3};
esp_err_t error = pipe.setFeatureReport(value, sizeof(value));
```

Sets the current value of a Feature report. It is returned the next time the
Host issues GET_REPORT. No interrupt transfer or `onFeatureReport` notification
happens.

- Callable after begin succeeds, from an application task or the USB worker. A value set before the Host configures the device is kept.
- The payload excludes the report ID. With `reportId` omitted (0), the Function's own ID is used. A Custom HID with several Feature IDs must name the ID.

| Return value | Condition |
| --- | --- |
| `ESP_OK` | Set |
| `ESP_ERR_INVALID_ARG` | `data` is null or `length` is 0, an ID belonging to another Function, or the ID is ambiguous |
| `ESP_ERR_INVALID_STATE` | Before start, or while stopping |
| `ESP_ERR_NOT_SUPPORTED` | No Feature report with that ID |
| `ESP_ERR_INVALID_SIZE` | Longer than declared |

### Receiving (Output / Feature)

- Output reports arrive both on the interrupt OUT endpoint (when there is one) and through SET_REPORT. Feature reports arrive through SET_REPORT.
- The callback receives an `EspUsbDeviceHidReport` (`reportId`, `reportType`, `data`, `length`). `data` is valid only during the callback; copy what you keep.
- Callbacks run on the USB worker. Do not block, call `device.end()`, destroy a Function or change settings there.

### Capacity queries

`inputReportCapacity()` / `outputReportCapacity()` / `featureReportCapacity()`
return the largest byte count the shared interface needs. Reports that declare a
report ID include its byte. They return 0 before start and after end.

`inputPacketSize()` returns the interrupt IN `wMaxPacketSize` at the speed the
Host negotiated. It is shared by every HID Function of a composite interface,
and is 0 until the Host resets the bus, and after end.

### Writing your own HID class

Derive from `EspUsbHidFunction`. The base handles begin/end, transfers,
allocation errors and stopping.

- Override `begin()` in the `protected` section. `device.begin()` calls it; the sketch does not. Validate there, then call the base `begin()` and return its result.
- If the derived class holds callbacks or response data, call `end()` first thing in its destructor, so members are destroyed only after the worker has stopped.

## Answering the Host

The library's own HID driver handles the following. The application does not
need to do anything.

| Request | Behaviour |
| --- | --- |
| GET_DESCRIPTOR (HID, Report) | Returns the HID descriptor and the report descriptor |
| GET_REPORT | Returns the latest value stored per ID and type. Initially zero-filled |
| SET_REPORT (Output/Feature) | Checks length and ID and STALLs on a mismatch; otherwise passes it to the callback |
| GET_IDLE / SET_IDLE | Kept per report ID. 0 means no automatic resend (the default) |
| GET_PROTOCOL / SET_PROTOCOL | Only on a boot interface. Bus reset returns to report protocol |
| SET_INTERFACE (alt 0), CLEAR_FEATURE(ENDPOINT_HALT) | Reset the transfer state and return the data toggle to DATA0 |

- **GET_REPORT values**: Input is updated when a send is accepted, Output/Feature when SET_REPORT completes. Sending a shorter payload also shortens what is stored.
- **Idle resend**: every (value × 4 ms) given by SET_IDLE, the stored Input is sent again (HID 1.11 §7.2.4). Relative values such as mouse X/Y/wheel are reset to 0 after a send, so a resend does not repeat the movement.
- **Protocol switching**: Keyboard switches between boot 8 bytes and NKRO 29 bytes; Mouse between boot 3 bytes and report 4 bytes. An old report in flight is stopped before the answer, so a report in the wrong format never reaches the Host.
- **Bus reset and deconfiguration** return Input/Output to neutral values and disable idle. Feature values are kept.
- **SET_INTERFACE or a HALT clear** may discard a report that was accepted but not yet completed. If delivery must be guaranteed, confirm and resend between the Host and your application.

## Buffers and memory

At begin the required sizes are worked out from the report descriptors, and
only the needed regions are allocated. No build settings are involved.

| Region | When and how large |
| --- | --- |
| IN DMA region | When there is an Input report. Largest Input report |
| OUT DMA region and assembly region | When there is an Output report and an interrupt OUT. Packet size and largest Output report |
| State storage | Sum of the report lengths per defined ID/type (for GET_REPORT and idle resend) |
| Control response region | Largest Input/Output/Feature |

- If an allocation fails, begin fails with `ESP_ERR_NO_MEM` and everything already allocated is returned.
- No heap is used during transfers. `device.end()` stops the USB worker and DMA before freeing.
- The library places DMA regions in internal RAM. HID has no API that takes a user buffer, so there is nowhere to choose PSRAM.
- A program that uses no HID does not link the HID driver or its buffers.

## Host OS side

HID works with each OS's standard driver. The table describes how each OS
generally handles HID. Interoperation of this version of the library with PCs
has not been checked.

| OS | Standard driver | Raw read/write from an application |
| --- | --- | --- |
| Windows | HidUsb (kbdhid for keyboards, mouhid for mice) | Vendor-defined reports can be opened with HIDAPI and similar. Keyboards and mice are owned by the OS and cannot be opened by applications |
| Linux | usbhid (`hidraw`, `input`) | `/dev/hidraw*`. Opening it as a normal user needs a udev rule |
| macOS | IOHIDFamily | HIDAPI and similar. Monitoring keyboard input needs a privacy permission |

How to check:

- Linux: `lsusb -v -d <vid>:<pid>` for descriptors, `sudo usbhid-dump` for the report descriptor, `evtest` for input events.
- Windows: USB Device Tree Viewer shows descriptors and status.
- Wrong characters: the Host turns usages into characters with its own layout. Set `setLayout()` to the Host's layout.
- Does not work in BIOS/UEFI: the Host uses boot protocol. Register the Keyboard and Mouse alone so that they are boot interfaces.

## Standards and design rationale

- **HID 1.11**: bcdHID in the HID descriptor is 1.11. Boot interfaces follow §4.2 and Appendix B, idle follows §7.2.4, protocol follows §7.2.5-7.2.6.
- **Why a composite HID is not a boot interface**: boot-format reports carry no report ID (HID 1.11 Appendix B). An interface that merges several Functions by report ID cannot coexist with the boot format, so its subclass/protocol are 0.
- **Gamepad hat**: the logical range is 1-8 and the Input has the Null state, so 0 means "not pressed" (HID 1.11 §6.2.2.5, Hat Switch in the HID Usage Tables).
- **System Control**: the usage is an array with logical minimum 1 and 0 as "not pressed" (the recommendation in HID 1.11 §6.2.2.5). On the wire it sends selectors 1/2/3, which the descriptor maps to 0x81/0x82/0x83.
- **Why allocation happens at run time**: large reports work without build settings, unused OUT/Feature resources are skipped, and the HID class is not linked at all when unused. The cost is parsing and allocation at begin, and explicit rejection of bad descriptors and over-long sends.
- **Why idle resend zeroes relative values**: so that a resend is not taken as new input and the pointer does not keep moving.

### Composite HID

When two or more HID Functions are registered, they are merged into one interface.

- Report IDs start from each Function's default (Keyboard 1, Mouse 2, Gamepad 3, Consumer 4, System 5, Vendor 6). A second Function of the same kind, and a Custom HID, get free IDs in registration order.
- The assigned Report ID is inserted right after the first Collection item of each Function's report descriptor (replacing a Report ID already at that position). A Custom HID that uses several Report IDs is safest registered alone.
- Each Function's descriptor is wrapped in a global Push/Pop, so its units and ranges do not leak into the next Function. This adds 2 bytes per Function.
- HID interfaces come before the interfaces of other classes.

## Changes from v2

- Build constants such as `CFG_TUD_HID` and `CFG_TUD_HID_EP_BUFSIZE` are gone. If they remain, the build stops with an error that points to the migration.
- System Control takes standard usages (0x81-0x83). Code that wrote the integers 1/2/3 must switch to the constants.
- Callback registration functions return `esp_err_t` and work only while stopped.
- Your own HID class derives from `EspUsbHidFunction`.

See [Migrating from v2](../migrating-from-v2.md) for details.

## Related examples

| Example | Content |
| --- | --- |
| [Keyboard](../../examples/Keyboard/) | Strings, switching layouts, LED state |
| [KeyboardNKRO](../../examples/KeyboardNKRO/) | NKRO (every key at once) |
| [Mouse](../../examples/Mouse/) | Mouse movement and buttons |
| [KeyboardMouse](../../examples/KeyboardMouse/) | Composite HID (told apart by report ID) |
| [Gamepad](../../examples/Gamepad/) | Axes, hat, buttons |
| [MediaKeys](../../examples/MediaKeys/) | Consumer Control |
| [CustomHID](../../examples/CustomHID/) | Your own report descriptor |
| [VendorHID](../../examples/VendorHID/) | Driverless raw data |
| [CompositeHidCdcMsc](../../examples/CompositeHidCdcMsc/) | HID with CDC and MSC |

Specifications: [USB HID 1.11](https://www.usb.org/sites/default/files/hid1_11.pdf), [HID Usage Tables](https://usb.org/sites/default/files/hut1_7.pdf)
