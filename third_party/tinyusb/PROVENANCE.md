# TinyUSB provenance and temporary dependency

[日本語](PROVENANCE.ja.md)

All v3 USB classes use independent drivers. The ten TinyUSB class sources and
unused class headers have been removed from the Arduino build tree. EP0/core,
FreeRTOS integration, DWC2 and some protocol definitions remain temporarily.
These are also scheduled for replacement; removing class sources is not the
completion of the stack migration.

- Pin and rationale: `UPSTREAM.json`, using a full commit SHA.
- License: MIT. Keep `LICENSE` and copyright/SPDX notices on remaining files.
- Selection: `BUILD_FILES.txt`, 27 files (5 sources and 22 headers).
- Sources: `tusb.c`, `common/tusb_fifo.c`, `device/usbd.c`,
  `portable/synopsys/dwc2/dcd_dwc2.c`, `portable/synopsys/dwc2/dwc2_common.c`.
- Only the currently used Audio/CDC/HID/MIDI protocol headers remain under
  `class/`. Unused DFU/MSC/NCM/Video definitions were also removed.
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

Remaining local patches support the temporary adapter:

- `device/dcd.h`, `device/usbd.c`, `device/usbd_pvt.h`: abort preserving DATA PID,
  nonblocking event fences reporting queue exhaustion, and actual control DATA
  byte counts.
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
