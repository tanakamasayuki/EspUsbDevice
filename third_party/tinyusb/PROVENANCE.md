# TinyUSB provenance

EspUsbDevice v2 vendors a selected TinyUSB device-stack source snapshot so its
USB configuration and runtime do not depend on Arduino-ESP32's prebuilt
`libarduino_tinyusb`.

- Upstream: https://github.com/hathach/tinyusb
- Commit: `53f8c53c2cbd73a91a172f1ae35e9abc00eb5075`
- Version macros: `0.21.0`
- License: MIT
- Archived source: complete upstream `src/` tree under `upstream/src/`
- Arduino build tree: upstream headers and the selected `.c` files under
  the library `src/` directory
- Local patches: none at initial import

This is the same TinyUSB commit recorded by the Arduino-ESP32 3.3.11 S2, S3,
and P4 tool packages:

```text
tinyusb: master 53f8c53c2
```

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

The complete upstream source tree is archived so files and relative includes
remain byte-identical to the pinned commit. Host, Type-C, DFU, video, printer,
MTP, MIDI 2.0, ECM/RNDIS, and non-ESP32 portable drivers are not part of the
initial build source set.

The `src/` build tree is a mechanical copy from the archived snapshot. Its
upstream files are not patched. `src/tusb_config.h` and
`src/internal/EspUsbTinyUsbConfig.h` are first-party integration files.

## Update rules

- Pin updates to a full upstream commit, not a moving branch or version string.
- Keep upstream copyright/SPDX headers unchanged.
- Record every local patch in this file.
- Run `python3 tools/verify_tinyusb_vendor.py` to verify that the Arduino build
  tree is byte-identical to this archive and contains only the selected sources.
- Run the host descriptor tests, S3/P4 compile tests, link-map symbol audit, and
  the complete hardware pytest suite before accepting an update.
- Do not copy Arduino-ESP32's `esp32-hal-tinyusb` integration into this tree.
