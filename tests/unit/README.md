# Unit Tests

> 日本語版: [README.ja.md](README.ja.md)

No board, no serial port, no Arduino CLI. Every module here either is plain
Python or extracts the shipped C++ from `src/` and compiles it with the system
g++, so the whole layer runs on a developer machine in about five seconds. That
is why CI runs it on every push, without `tests/.env` - see
`.github/workflows/unit-tests.yml`.

Tests that call the real library API on a real chip live in `../single/`
instead. They look like unit tests and several of them start TinyUSB only to
leave it stopped, but they need a board, so keeping them here made `unit/`
un-runnable in CI and hid that fact behind an upload error.

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
Core Kconfig, S2/S3 compile for full-speed capacity, P4 compiles for
full/high-speed capacity, and the Audio compile-time limits hold. The
controller/root-hub port and actual bus speed are left to runtime
initialization.

## `tinyusb_vendor`

Checks that the TinyUSB pin metadata, headers, and selected device sources
vendored under `src/` remain byte-identical to the upstream commit named in
`third_party/tinyusb/UPSTREAM.json`, and that no unintended `.c` file has
entered the build. It fetches that upstream tarball on a cache miss, which is
the only thing in this layer that touches the network.

## `audio_model`

Exercises the v2 PCM format and bandwidth model without the removed Audio
implementation. It covers mono/stereo, 16/24/32-bit samples, subslots, FS/HS
frame rates, clock tolerance, isochronous packet and software-buffer limits,
the entity graph, UAC2 descriptors, Clock/Feature control state, and CUR/RANGE
wire formats.

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

## `midi_descriptor`

Host g++ test for the multi-cable USB MIDI configuration descriptor. The builder
no longer uses TinyUSB's single-cable `TUD_MIDI_DESCRIPTOR()` template - it emits
the head, the per-cable jack descriptors, and the endpoint blocks itself - and
that assembly fails silently: a host that reads a wrong `wTotalLength` or a
duplicated jack ID still enumerates and just shows the wrong number of ports, so
a round trip on real hardware passes while the descriptor is wrong. The builder
is extracted from `src/EspUsbDevice.cpp` at test time and compiled against the
real TinyUSB macros and enums, so the assertions run against the shipped code.

## `dependency_boundary`

Greps the shipped sources for the Arduino-ESP32 USB core headers and symbols the
library must not depend on. It is a boundary this project decided once and would
otherwise re-cross by accident, since including `USB.h` compiles perfectly well
and only shows up as a conflict at runtime.

## `known_findings`

Checks the serial-log allowlist in `tests/conftest.py` against the tests that
actually exist. Its rules are keyed on pytest node ids with nothing connecting
them to the tests they name, so a rename or a merge detaches a rule silently -
the test still passes, and the expected line it covered comes back as an
unexpected finding. That happened when `peer/` went from 110 tests to 29.
