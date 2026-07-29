# TinyUSB provenance

[日本語](PROVENANCE.ja.md)

EspUsbDevice v2 vendors a selected TinyUSB device-stack source snapshot so its
USB configuration and runtime do not depend on Arduino-ESP32's prebuilt
`libarduino_tinyusb`.

- Pin metadata: `UPSTREAM.json` (repository, full commit SHA, TinyUSB version,
  reviewed Arduino-ESP32 baseline, and selection reason)
- License: MIT
- Arduino build tree: only the selected `.c` files and their S2/S3/P4
  transitive headers under the library `src/` directory
- Build manifest: `BUILD_FILES.txt` (43 files: 12 sources and 31 headers)
- Verification cache: the pinned upstream tarball and its 43 selected files are
  downloaded on demand under ignored `.upstream-cache/`
- Local patches: none at initial import

The verification cache is downloaded from the canonical
[`hathach/tinyusb`](https://github.com/hathach/tinyusb) repository.
[`espressif/tinyusb`](https://github.com/espressif/tinyusb) is a fork of that
repository and also contains this exact Git commit. Because the full commit SHA
identifies the same Git tree in both repositories, the selected source contents
are identical; the canonical upstream is used as the download origin.

## Initial build source set

Core:

- `src/tusb.c`
- `src/common/tusb_fifo.c`
- `src/device/usbd.c`

Classes:

- `src/class/hid/hid_device.c`
- `src/class/cdc/cdc_device.c`
- `src/class/midi/midi_device.c`
- `src/class/msc/msc_device.c`
- `src/class/vendor/vendor_device.c`
- `src/class/net/ncm_device.c`
- `src/class/audio/audio_device.c`

ESP32 DWC2 device controller:

- `src/portable/synopsys/dwc2/dcd_dwc2.c`
- `src/portable/synopsys/dwc2/dwc2_common.c`

The complete upstream source tree is not stored in this repository. The
verification script downloads the tarball for the pinned commit when its
ignored local cache is absent, extracts only the 43 manifest entries, and
compares them byte-for-byte with the Arduino build tree. Normal Arduino builds
never download anything. The build tree is a minimal projection measured from
clean S2, S3, and P4 compiler dependency files. Host, Type-C, DFU, video,
printer, MTP, MIDI 2.0, ECM/RNDIS, non-FreeRTOS OSALs, and non-ESP32 portable
files are not copied into the build tree.

The `src/` build tree is a mechanical copy from the pinned snapshot. Its
upstream files are not patched. `src/tusb_config.h` and
`src/internal/EspUsbTinyUsbConfig.h` are first-party integration files.

## Update rules

### Review cadence

- Review the pin whenever the supported Arduino-ESP32 baseline changes. Prefer
  the TinyUSB commit recorded by that core's S2, S3, and P4 tool packages.
- Also review it before each major/minor EspUsbDevice release and at least once
  per quarter when neither the core nor the library release cadence triggers a
  review. A review does not require an update.
- Update between those reviews only for a relevant upstream bug, security fix,
  required USB feature, or target support. Record any deliberate divergence
  from the core-bundled commit in `UPSTREAM.json` `selection_reason`.
- Pin a full upstream commit, never a moving branch or version string.

### Update procedure

1. Install/select the new Arduino-ESP32 baseline and inspect the `tinyusb:`
   entries in the S2, S3, and P4 tool-package `versions.txt` files. They must be
   considered separately; do not assume every target uses the same commit.
2. Edit `UPSTREAM.json`: full `commit`, `tinyusb_version`,
   `reviewed_with_arduino_esp32`, and `selection_reason`. Changing the commit
   changes the cache directory, so the next verification/update command
   downloads that commit automatically.
3. Run `python3 tools/verify_tinyusb_vendor.py`. A new pin normally reports
   modified selected files after populating the cache; that failure is the
   expected review point.
4. Run `python3 tools/update_tinyusb_vendor.py` for a dry-run file list, review
   the upstream changes, then run
   `python3 tools/update_tinyusb_vendor.py --apply`.
5. Clean-compile the complete example matrix for S2, S3, and P4. If compiler
   dependencies changed, regenerate `BUILD_FILES.txt` from clean dependency
   files, copy any newly required upstream headers, remove no-longer-used ones,
   and repeat the compile until all three targets are clean.

   ```sh
   cd tests
   uv run --env-file .env pytest examples_compile/ --clean -vv
   ```

6. Run `python3 tools/verify_tinyusb_vendor.py` again. It must pass
   byte-for-byte before testing on hardware.
7. Run the complete hardware suite:

   ```sh
   uv run --env-file .env pytest --clean
   ```

8. Review the full diff, upstream license/SPDX headers, link-map symbol audit,
   and provenance. Keep upstream headers unchanged and record every local patch
   here. Do not copy Arduino-ESP32's `esp32-hal-tinyusb` integration.
