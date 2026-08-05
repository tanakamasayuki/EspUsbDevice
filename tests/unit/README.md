# Unit Tests

> 日本語: [README.ja.md](README.ja.md)

Unit tests cover host-independent logic:

- Device descriptor bytes.
- Configuration descriptor layout.
- FS/HS endpoint MPS selection.
- HID report descriptor bytes.
- HID keyboard/mouse report builders.
- MSC FAT RAM disk helper boot sector, FAT, root directory, and file-read
  helpers.

## `compile_smoke`

This is the first environment check. In `--run-mode=build`, it verifies Arduino
CLI, sketch.yaml, the ESP32 board package, library resolution, and minimal
public header compilation. It does not validate the USB device stack at runtime.

## `descriptor`

This verifies USB device, configuration, and HID report descriptor bytes. The
initial spec fixes HID keyboard and HID mouse interrupt endpoint MPS to 8 bytes
for both FS and HS. Keyboard + mouse composite uses one HID interface with
report IDs and 16-byte endpoint MPS so the report-ID-prefixed keyboard report
fits in one interrupt packet.

## `ccid_descriptor`

Host g++ test for the CCID interface and class descriptor. The descriptor
builder is pure byte assembly with no Arduino or TinyUSB dependency, so it is
extracted from `src/EspUsbDeviceCcid.cpp` at test time (as `keymap` does) and
compiled on the host - the assertions run against the shipped code. It checks
every CCID class descriptor field, most importantly the exchange level a host
reads to decide between TPDUs and APDUs, and that the interrupt endpoint sits
one above the bulk pair in both standalone and composite placement.

## `descriptor_model`

Exercises the v2 descriptor foundation using host g++ only, without Arduino or
TinyUSB headers. It checks buffer bounds, interface/string allocation,
directional and duplex endpoint allocation, conflict/capacity errors, and
FS/HS endpoint MPS selection, other-speed configuration, the device qualifier,
and the HID function writer. It remains runnable while the Arduino sketch is
temporarily unbuildable during the v2 rewrite.

## `tinyusb_config`

Host-compiles the library-owned TinyUSB configuration for the S2, S3, and P4
target macros. It verifies that all device classes are enabled without Arduino
Core Kconfig, S2/S3 compile for full-speed capacity, and P4 compiles for
full/high-speed capacity. The controller/root-hub port and actual bus speed are
left to runtime initialization.

## `tinyusb_vendor`

Checks that the TinyUSB pin metadata, headers, and selected device sources in the Arduino build
tree remain byte-identical to the pinned archive, and that no unintended `.c`
file has entered the build.

## `audio_model`

Exercises the v2 PCM format and bandwidth model without the removed Audio
implementation. It covers mono/stereo, 16/24/32-bit samples, subslots, FS/HS
frame rates, clock tolerance, isochronous packet and software-buffer limits,
the entity graph, UAC2 descriptors, Clock/Feature control state, and CUR/RANGE
wire formats.

## `audio_v2_descriptor`

Builds the new public `EspUsbAudioFunction` API on S3 hardware and checks the
speaker, microphone, and duplex configuration descriptors, FS/HS packet sizes,
polling of mute, volume, and stream-state events, and the stream-stats reset
lifecycle. UAC1 24-bit and 32-bit formats also verify subslot/bit fields, packet
sizes, and transfer accounting. It leaves the USB runtime stopped, so this
specifically tests public API, device-descriptor, and control state integration.

## `p4_controller_endpoints`

Runs on P4 without starting TinyUSB and verifies controller-specific descriptor
limits: a five-IN-endpoint composite is rejected for the FS controller but
accepted for HS and for P4's HS-default `Auto` selection.

## `keymap`

This is a pure host g++ test (no board required). It extracts the layout enum,
the `ESP_USB_DEVICE_MOD_*` constants, the keymap includes, and the pure
`espUsbDeviceAsciiToUsage` reverse-lookup function verbatim from the real
`src/EspUsbDevice.{h,cpp}` at run time, compiles them with `keymap_test.cpp`, and
checks the character -> HID usage+modifier round-trip: base/Shift levels, the
AltGr (Right Alt) fallback (`@` on de_DE, `{ [ ] }` etc.), and the pt_BR 0x90
tableSize fix (`/` and `?` on International1, usage 0x87). The keymap tables in
`src/keymap/*.h` are byte-identical to EspUsbHost's, whose forward direction is
covered by that library's own keymap test.

## `nkro_report`

This is a pure host g++ test (no board required) for the NKRO held-key state
`EspUsbDeviceNkroKeyboardReport`. The struct is header-only, so the test extracts
it verbatim from `src/EspUsbDevice.h` at run time and compiles it, checking the
bitmap layout (bit `usage & 7` of byte `usage >> 3`), the routing of modifier
usages `0xE0`-`0xE7` into `modifiers`, the `MaxBitmapUsage` (`0xDF`) boundary with
`0xE8` and above rejected, ten simultaneous keys, `clear()`, and copy semantics.
The extraction fails loudly if the struct ever starts depending on Arduino or
TinyUSB, since the host build would no longer be meaningful. Same technique as
`keymap`, so the test cannot drift away from the shipped struct. The
boot-protocol fold-down, the "no `enableNkro()` -> fail" rule, and the bytes that
actually reach a host belong to `EspUsbDeviceHidKeyboard`, which cannot be
host-compiled; `tests/peer/hid_keyboard_nkro` covers those on hardware.

## `fat_ramdisk`

This verifies host-independent `EspUsbDeviceMscFatRamDisk` logic:

- FAT12 boot sector fields.
- Volume label, FAT type, and boot signature.
- 8.3 filename normalization.
- Root directory entries.
- FAT12 cluster chains.
- `exists()`, `fileSize()`, and `readFile()`.
- `EspUsbDeviceMsc` attach, read/write callbacks, and eject callback.
