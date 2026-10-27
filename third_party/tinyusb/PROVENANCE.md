# TinyUSB provenance and temporary dependency

[日本語](PROVENANCE.ja.md)

All v3 USB classes use independent drivers. The ten TinyUSB class sources and
unused class headers have been removed from the Arduino build tree. The independent EP0/core also replaces usbd.c; the old bootstrap and FIFO
implementations were removed. The device controller now uses the owned
`src/internal/EspUsbController.h` contract. DWC2 and some wire/protocol
definitions remain temporarily.
These are also scheduled for replacement; removing class sources is not the
completion of the stack migration.

- Pin and rationale: `UPSTREAM.json`, using a full commit SHA.
- License: MIT. Keep `LICENSE` and copyright/SPDX notices on remaining files.
- Selection: `BUILD_FILES.txt`, 12 files (2 sources and 10 headers).
- Sources: `portable/synopsys/dwc2/dcd_dwc2.c` and
  `portable/synopsys/dwc2/dwc2_common.c`.
- No vendor class headers remain. The required US keyboard lookup rows were
  extracted into `src/keymap/keymap_en_us.h` with MIT attribution; unused HID
  protocol definitions and templates were removed.
- `src/tusb_config.h` and `src/internal/EspUsbTinyUsbConfig.h` are first-party
  integration. Enabling the legacy class drivers is rejected.

The v2 selection originally included nine classes, then ten after adding Video.
Git preserves that history and the removed Windows NCM divisor patch. Independent
NCM also uses four-byte alignment; no patch for a deleted source remains in the
current manifest.

## Verification and local patches

`python3 tools/verify_tinyusb_vendor.py` checks the manifest, compiled source set
and exact upstream contents. It obtains only the selected files from the pinned
canonical [hathach/tinyusb](https://github.com/hathach/tinyusb) tarball when the
ignored `.upstream-cache/` is absent. Normal Arduino builds never download files.
The cache stays unchanged; exact replacements in `LOCAL_PATCHES.json` are applied
for comparison. The update script uses the same manifest and does not restore
removed classes.

Remaining local patches support the temporary adapter. The patch for removed
usbd.c is also retained only in Git history:

- The removed device DCD/private, FIFO, OS abstraction and umbrella headers
  remain only in Git history. The controller event layout in the owned header
  is adapted from TinyUSB `device/dcd.h`, retaining its MIT attribution.
- `portable/synopsys/dwc2/dcd_dwc2.c`: stop without STALL and split DMA arms by
  actual GHWCFG3 counter widths. S3 has seven packet-count bits although the
  common register type exposes ten. Intermediate arms emit no completion;
  a short/ZLP or full transfer completion returns the result once.

Selected paths have been tested on S3–S3. P4/S2/S31 hardware validation remains
pending. Review these replacements when changing the upstream pin; each original
must match exactly once. Preserve provenance and licenses when moving derived
code into the independent implementation.

## Update procedure

1. When the core baseline or a relevant fix requires review, inspect each SoC
   tool package's `versions.txt` and remaining dependencies. Record a full commit
   in `UPSTREAM.json`, never a moving branch.
2. Run `python3 tools/verify_tinyusb_vendor.py` and review the differences.
3. Preview `python3 tools/update_tinyusb_vendor.py`; use `--apply` only for the
   reviewed manifest files.
4. Review remaining upstream contents, patches, licenses, compiler dependencies
   and linked symbols.
5. Run the standard plugin's `--combo all` from `validation/`, distinguishing
   untested SoCs/HS. Do not restore old `tests/` or `examples/` as v3 design gates.


The DWC2 adapter also accepts core-owned runtime storage through the first-party
`src/internal/EspUsbDwc2Runtime.h` boundary. Static transfer/controller/SETUP
storage is replaced with compact records for descriptor-selected directions.
This is recorded in LOCAL_PATCHES.json, retains the original MIT attribution,
and is an intermediate lifecycle boundary rather than completion of the DCD
replacement. Physical FIFO reservations and controller shutdown remain intact.

The runtime adapter cleans and invalidates the dedicated SETUP line before initial DMA, publishing its pointer only after synchronization succeeds. Failed preparation leaves storage unpublished for core cleanup.

The recorded DWC2 patches also wait for endpoint shutdown, clear latched non-control interrupts, and serialize endpoint ISR/task operations. SOF class callback dispatch stays outside that lock.


The native controller API has typed runtime options and device-only init, and
uses `espusb_controller_*` symbols. The ESP adapter uses only internal DMA;
unreachable slave/FIFO code and its transfer-record pointer were removed.
The core still owns queue/fence capacity and FreeRTOS synchronization directly.
The pinned updater cannot restore removed device/private, OS/FIFO or umbrella
headers. DWC2 register/port sources and wire/protocol definitions remain derived
upstream dependencies; this boundary change is not full DWC2 replacement.


The native bus/control/endpoint boundary uses `espusb_device_*` symbols rather
than exporting the old `tud_*`/`usbd_*` entry points. Unused legacy bootstrap,
task-pump and user-SOF hooks were removed; selected class SOF callbacks still
run in the controller event path. CDC/MIDI descriptor layouts were migrated
from the former TinyUSB templates in `device/usbd.h` (Copyright (c) 2019
Ha Thach, MIT; license retained here). MIDI builds its runtime cable graph and
length directly rather than patching a symmetric single-cable macro result.
This does not complete replacement of the shared wire types or DWC2 sources.


Native core/classes now use `src/internal/EspUsbWire.h`: explicit request,
interface and endpoint layouts, raw-byte request/endpoint attributes, LE reads,
and native controller speed/results. There are no aliases to vendor wire types.
The remaining DWC2 adapter consumes these native boundary types, but retains
upstream register/common helpers internally. The US key table is derived from
TinyUSB HID_KEYCODE_TO_ASCII (Copyright (c) 2019 Ha Thach, MIT).
VBUS policy is passed from EspUsbDeviceConfig::vbusSensing at runtime; legacy
VBUS build flags are rejected by the temporary capability integration.
