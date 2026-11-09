# A pending HID IN across a protocol switch

> 日本語: [README.ja.md](README.ja.md)

Checks that switching between boot and report protocol never hands the Host an old-format IN report it has not read yet.
Runs at FS on S3 / S2 (combos `s3`, `s3_s2`, `s2_s3`) and on the P4 / P4X HS link (`p4_p4x_hs`, `p4x_p4_hs`).

## Purpose

- Even if SET_PROTOCOL arrives while an old-format report is still in the Device's DMA, the first report the Host reads after the switch is in the new format.
- A send whose length differs from the current format is refused with `InvalidSize`.

## Steps

- For this probe only, the Host claims the interface through the public ESP-IDF USB Host API.
  The usual EspUsbHost polls HID IN automatically, so it cannot reliably leave an old-format report in the Device's DMA.
- The Host posts no interrupt IN until the protocol is switched. The Device's second non-blocking send being refused shows that the first report is still pending.
- Keyboard (NKRO 29 bytes / boot 8 bytes) and Mouse (report 4 bytes / boot 3 bytes) are each switched four times.
- After the switch the Host posts its first IN and checks that it does not complete for 30 ms (the old report never arrives).
- A report with a new marker is then sent into that IN, and its length and every byte are matched.
- The core enables `CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK`, so an `enum_filter_cb` that allows the target VID/PID is registered; with null, enumeration is aborted.
- At the end the transfer is returned and the interface, device, client and Host are released. DMA areas not yet returned are never freed.

## Out of scope

- A full core event queue, or injected artificial stale completion events
- A bus reset or `end()` in the middle of an IN transfer

References: [HID 1.11](https://www.usb.org/sites/default/files/hid1_11.pdf) §7.2.6 (Set_Protocol), [ESP-IDF USB Host](https://docs.espressif.com/projects/esp-usb/en/latest/esp32s3/usb_host.html)
