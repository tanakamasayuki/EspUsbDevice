# Typed HID sends

> 日本語: [README.ja.md](README.ja.md)

Checks that the Gamepad, Consumer Control and System Control send APIs produce reports whose meaning matches the descriptor.
Runs at FS on S3 / S2 (combos `s3`, `s3_s2`, `s2_s3`) and on the P4 / P4X HS link (`p4_p4x_hs`, `p4x_p4_hs`).

## Purpose

- The raw input and the GET_REPORT value each API produces match a known byte sequence.
- The Host parses the report descriptor it read over EP0, and the meaning of each value (usage, unit, range) is right.
- Invalid values are refused and produce no extra input.

## Steps

- Gamepad, Consumer and System on their own, and all three in one HID, each started twice.
- Gamepad: six signed axes, the hat, 32-bit buttons, both the struct and the argument forms, `releaseAll`.
  The hat is checked at up / right / up-left and at centre as Null (the Null of a variable field).
- Consumer / System: `press`, `sendUsage`, `release`, `click`, `usage()`. `click` produces two reports, press and release.
- System Control is given the standard usages 0x81 / 0x82 / 0x83 and matched against selectors 1 / 2 / 3 on the wire, and 0 on release.
  0 is checked as the array's "nothing pressed" (the HID 1.11 rule for arrays).
- In the combined configuration a custom HID with units and a physical range is registered first, and its global items must not leak into the Gamepad.
- Invalid API calls are refused; `usage()` and GET_REPORT stay unchanged and the Host sees no extra input while it watches.

## Out of scope

- The Host is EspUsbHost on an S3 / P4. Compatibility with PC operating-system controls or games is not checked.

References: [HID 1.11](https://www.usb.org/sites/default/files/hid1_11.pdf), [HID Usage Tables](https://usb.org/document-library/hid-usage-tables-15)
