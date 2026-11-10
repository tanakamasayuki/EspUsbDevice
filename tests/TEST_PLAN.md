# Test plan

> 日本語: [TEST_PLAN.ja.md](TEST_PLAN.ja.md)

What the hardware tests have verified, what they have not, and the facts the expected values rest on.
How to run them is in [README.md](README.md); remaining work is in the [TODO](../TODO.ja.md).

## Verified

| Scope | Combo | Verified |
| --- | --- | --- |
| S3 ↔ S3 (FS) | `s3` | Every `peer/` probe: ten classes, lifecycle, recovery from allocation failure, cancel / reset / end races, standard requests, suspend / resume, remote wakeup |
| S2 ↔ S3 (FS, both directions) | `s3_s2`, `s2_s3` | Every `peer/` probe, with the S2 as Device and as Host |
| P4 v1.x loopback (FS, both controllers) | `p4`, `p4_psram` | Basic traffic of the ten classes, borrowed DMA up to 65537 bytes, restart; MSC storage and Vendor rings in PSRAM |
| P4 v1.x ↔ P4X HS link (both directions) | `p4_p4x_hs`, `p4x_p4_hs` | Every `peer/` probe plus the HS-only probes: HS enumeration, MPS 512, borrowed transfers, Audio / Video isochronous, storage in PSRAM |
| Linux PC → P4X (HS) | `p4x_pc` | MSC media change (06/28/00, 02/3A/00, residue), a FAT RAM disk passing `fsck.fat`, DFU DETACH |
| Linux PC → S2 (FS) | `primary=s2_pc` | MSC media change, DFU DETACH. Not a permanent pair |
| Build | — | Compiles for S2 / S3 / P4 / P4X; unused classes are not linked |

## Not verified

| Scope | State |
| --- | --- |
| ESP32-S2 entering the ROM DFU | `rebootToRomDfu()` not checked |
| ESP32-S31 | Not supported |
| Windows / macOS | Neither class binding nor class behaviour checked |
| Linux PC class drivers | Only the MSC / DFU wire checks above; CDC / HID / Audio / Video / NCM with the OS drivers not checked |
| Real SD cards and OTA | RAM backends only |
| Long continuous runs | Up to a fixed number of repetitions, reconnections included |
| VBUS detection on a real unplug (`vbusSensing=true`) | Not checked |
| USB DMA reading or writing PSRAM directly | Out of scope: the product refuses it |
| Arduino-ESP32 4.x | Not checked; the baseline is 3.3.12 |
| Every race order (all combinations of SETUP / reset / end / SET_CONFIGURATION) | Representative orders only |

## Probes for the public API contract

The API promises written in the documentation are checked on the wire by these probes.

| Probe | Checks |
| --- | --- |
| `peer/api_semantics` | Keyboard presses adding up, modifier bits, compaction after a release, a tap releasing only what it added, `releaseKey()`, a seventh key refused. Mouse `move()` keeping buttons and refusing out-of-range values. HID packet lengths (`inputPacketSize()`; an `EspUsbDeviceHidCustom` `inputReportSize` of 200 is 64 at FS, 200 at HS). Vendor requests that share a bRequest with WebUSB / MS OS 2.0 reaching the sketch. `end()` returning `ESP_ERR_TIMEOUT` while a callback does not return, and being called again. TEST_MODE accepted only at HS |
| `peer/vendor_packet_size` | Vendor FS bulk sizes 8 / 16 / 32 / 64 work and `begin()` refuses any other; HS is always 512 |
| `peer/fifo_budget` | At HS five default Vendors enumerate and a sixth makes `begin()` return `ESP_ERR_INVALID_SIZE` |
| `pc/msc_fat` | The PC reads `EspUsbDeviceMscFatRamDisk` volumes of 1 / 4 / 24 MiB and checks them with `fsck.fat` |
| `peer/direct_queue` | Two borrowed sends may be outstanding: a third is `Busy`, and sending, cancel and `end()` return both once, in order |
| `peer/fifo_budget` | Per-Function `bulkInBuffering`: only the Function set to `Double` is doubled, and `begin()` refuses a `Double` that cannot fit |

## Values that change at high speed

On an HS-capable controller the bulk MPS goes from 64 to 512, and the expected values change with it.

| Item | FS | HS |
| --- | --- | --- |
| Bulk MPS | 64 | 512 |
| MSC `transferBytes` | A multiple of 64 | A multiple of 512 (otherwise `begin()` returns `ESP_ERR_INVALID_SIZE`) |
| Default CCID `messageBytes` | 271 | 512 |
| Borrowed receive capacity | A multiple of 64 | A multiple of 512 |
| Non-control IN endpoints | 4 | 7 |
| HID interrupt endpoint | Up to 64 bytes | Up to 1024 bytes |
| UAC2 feedback | 10.14 in 3 bytes | 16.16 in 4 bytes |

- An HS-capable controller checks bulk capacities against 512 even when the link runs at FS. That is why the P4 loopback refuses an MSC staging of 64.
- CDC / Vendor / MIDI accept a `transferBytes` below the MPS.
- `begin()` checks that every IN endpoint of the configuration fits the TX FIFO and returns `ESP_ERR_INVALID_SIZE` if not. On the P4 HS controller five default Vendors fit and the sixth fails.
- The Device reads the link speed with `device.speed()`.

Limits on the Host side (the ESP-IDF USB Host) also shape the tests.

- A bulk IN `num_bytes` must be a multiple of the MPS.
- `usb_host_endpoint_clear` does not reset the Host's data toggle, so a HALT test sends an even number of packets before the clear.
- At HS the Host's default periodic OUT FIFO (512 bytes) can be too small for HID.
- Vendor IN is received through the released EspUsbHost's cache-prepared read queue and callback. The synchronous `vendorReadSync()` is not used because of a cache problem on the P4.

## P4 and P4X build settings

Nothing in the product source tells a P4 from a P4X; they differ only in build settings and SDK.

| Item | P4 (revision v1.x) | P4X (revision v3.x) |
| --- | --- | --- |
| FQBN | `esp32:esp32:esp32p4:ChipVariant=prev3,USBMode=default,CDCOnBoot=default` | `esp32:esp32:esp32p4:ChipVariant=postv3,USBMode=hwcdc,CDCOnBoot=cdc` |
| `Serial` path | UART0 → USB-UART | USB Serial/JTAG (HWCDC) |
| Library package | `esp32p4_es-libs` | `esp32p4-libs` |
| Revisions the SDK supports | v1.x | v3.1 or later |
| CPU clock (build setting) | 360 MHz | 400 MHz |
| HS non-periodic / periodic TX queue depth | 4 / 4 | 8 / 16 |
| HS endpoints / IN endpoints / FIFO | 15 / 8 / 1024 words | Same |

- The bench P4X logs over USB Serial/JTAG, so its test profiles use `USBMode=hwcdc`. With `USBMode=default`, `Serial` goes to UART0 and no log reaches the control port. The controller under test is the HS controller, a different pin pair from USB Serial/JTAG.
- The examples' `esp32p4x` profile uses `USBMode=default`; it is not the test profile.
- The P4X SDK supports revision v3.1 or later; a v3.0 chip is not treated as supported.
- The endpoint count and FIFO are the same, but a pass on the P4 is never taken as a pass on the P4X; both are tested.

## PSRAM

- Every P4 bench board has 32 MiB of PSRAM (measured by `single/psram`).
- Test profiles that use PSRAM add `PSRAM=enabled` to the FQBN (`p4_psram`, the Device of `peer/psram_link`). Other P4 profiles leave PSRAM off.
- The product can put CPU-accessed areas such as rings and MSC storage in PSRAM. The staging the USB DMA touches stays in internal RAM.
- A PSRAM buffer passed to a borrowed transfer (`readDirect` / `writeDirect`) is refused with `NotDmaCapable`; the tests are written on that basis.
