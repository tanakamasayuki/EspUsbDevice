# Single-Board Tests

> 日本語版: [README.ja.md](README.ja.md)

Tests that exercise the library on one device board, with no USB host board
involved. Everything here uploads a sketch that calls the real API on the real
chip and prints `OK` or `NG`; the pytest side only reads that verdict.

Most of these need the chip rather than a bus: they set
`config.startTinyUsb = false`, so `begin()` builds the descriptors and stops,
and the sketch then asserts on the bytes. What they cannot do is run on a PC,
because the library is Arduino/ESP-IDF code. Where a builder is pure byte
assembly with no Arduino or TinyUSB dependency, the host-compiled technique in
`../unit/ccid_descriptor` and `../unit/midi_descriptor` is the better home - it
runs in CI and asserts against the same shipped source.

`p4_controller_endpoints` is the exception that will always belong here: the
endpoint budget it checks is a property of the P4's controller.

These need the ports in `tests/.env`, like `peer/` and `loopback/`:

```sh
uv run --env-file .env pytest single/
```

## `compile_smoke`

This is the first environment check, and it works at two levels. Under
`--run-mode=build` it stops at the build and verifies Arduino CLI, sketch.yaml,
the ESP32 board package, library resolution, and minimal public header
compilation. In a normal run it also uploads and executes, so it additionally
says that every public class links and can be constructed and configured on the
chip. It does not exercise the USB device stack: nothing here enumerates.

The build half of that is also covered by `tools/build_check.py` and the CI
Build Check workflow, which compile every example for every profile. What only
this module adds is the on-target half - a link and construct that a compile
cannot prove.

## `descriptor`

This verifies USB device, configuration, and HID report descriptor bytes. The
initial spec fixes HID keyboard and HID mouse interrupt endpoint MPS to 8 bytes
for both FS and HS. Keyboard + mouse composite uses one HID interface with
report IDs and 16-byte endpoint MPS so the report-ID-prefixed keyboard report
fits in one interrupt packet.

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

## `fat_ramdisk`

This verifies host-independent `EspUsbDeviceMscFatRamDisk` logic:

- FAT12 boot sector fields.
- Volume label, FAT type, and boot signature.
- 8.3 filename normalization.
- Root directory entries.
- FAT12 cluster chains.
- `exists()`, `fileSize()`, and `readFile()`.
- `EspUsbDeviceMsc` attach, read/write callbacks, and eject callback.

## `cdc_multi`

Two CDC ACM ports on one S3 device, checked as descriptor bytes rather than as
traffic: interface and association counts, the IN/OUT endpoint addresses each
port draws, which TinyUSB instance each `EspUsbDeviceCdcSerial` object drives,
the per-port name published as `iFunction`, and the rejection that happens when
a sketch registers more ports than the controller's IN endpoint budget allows.
`tests/peer/usb_serial_multi` drives the same device with real traffic.

## `composite_constraints`

Builds every Audio + HID / CDC / Vendor combination with
`config.startTinyUsb = false` and pins which ones are accepted, which are
rejected, and where the `MAX_CLASSES` guard fires. What bounds a composite is
the controller's non-control IN endpoint budget rather than a class count, so
this is the regression fence for that arithmetic.

## `p4_hs_packet_sizes`

Checks the endpoint packet sizes an ESP32-P4 build hands out at high speed,
where the legal maxima differ from full speed per endpoint type. It is the fence
around the speed-dependent half of endpoint allocation, which a full-speed-only
board cannot exercise.

## `dfu_descriptor`

The DFU function's descriptor bytes in both Download and Runtime mode, on the
chip: the functional descriptor fields, that the interface costs **zero**
endpoints (which is what lets DFU attach to a device whose endpoint budget is
spent), the cost of adding it to a composite, its function string, that a second
DFU function is rejected, and the OTA partition it would write into - checked
against the real partition table.

## `msc_firmware_disk`

The firmware drive that `EspUsbDeviceMscFirmwareDisk` publishes, built and
inspected with `startTinyUsb = false`: geometry and FAT contents, the README
file a host sees, that non-image writes are ignored, the ascending-order rule,
the directory-entry commit, that verification refuses a bad image, and that the
boot partition does not move. The UF2 container is covered here too - blocks
written in reverse order, a duplicated block not counted twice, and a wrong
family ID refused.

## `bulk_in_fifo`

Which bulk IN endpoints get a two-packet controller transmit FIFO. No bits
without a bulk IN endpoint; one bulk IN always doubled; in a composite the bits
are a subset of the declared bulk IN endpoints and are all-or-nothing;
`EspUsbBulkInBuffering::Single` forces it off and `Double` either applies or is
refused. The budget is the DWC2 DFIFO, not RAM, so this is the fence that keeps
an over-ambitious default from producing a device that will not enumerate.

## `rom_dfu_guard`

`rebootToRomDfu()` must return `false` with `ESP_ERR_NOT_SUPPORTED` **without
restarting** on an ESP32-S3 whose `USB_PHY_SEL` eFuse is not burned - the ROM
would otherwise put its DFU stack on a controller with no pads and the connector
would go dark. Reaching the line after the call is half the assertion. The test
reads the same eFuse first and skips rather than asserting on a board where the
call would really restart; a board logging over USB Serial/JTAG cannot have it
burned, because burning it takes USB Serial/JTAG away. See
[`tests/manual/s3_single_cable`](../manual/s3_single_cable/) for the
host-observable half.
