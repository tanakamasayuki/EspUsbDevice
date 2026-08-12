# EspUsbDevice 2.1.0 — arduino-esp32 core compatibility

- Library ref: `v2.1.0`
- Targets: esp32s3, esp32s2, esp32p4
- Core versions: 3.3.0, 3.3.1, 3.3.2, 3.3.3, 3.3.4, 3.3.5, 3.3.6, 3.3.7, 3.3.8, 3.3.9, 3.3.10, 3.3.11
- Official support floor: arduino-esp32 3.3.9

> Results below arduino-esp32 3.3.9 are informational build observations and do not indicate official support.

Legend: ✅ builds · ❌ fails · — example absent in this version · · not applicable (no profile / board not in core)

## esp32s3

| Feature (example) | 3.3.0 (info) | 3.3.1 (info) | 3.3.2 (info) | 3.3.3 (info) | 3.3.4 (info) | 3.3.5 (info) | 3.3.6 (info) | 3.3.7 (info) | 3.3.8 (info) | 3.3.9 | 3.3.10 | 3.3.11 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| HID (`Mouse`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Serial (`Serial`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| MIDI (`MIDI`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Storage (`MSC`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Audio (`AudioSpeaker`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Vendor (`USBVendor`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Network (`UsbNetwork`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Composite (`CompositeHidCdcMsc`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

## esp32s2

| Feature (example) | 3.3.0 (info) | 3.3.1 (info) | 3.3.2 (info) | 3.3.3 (info) | 3.3.4 (info) | 3.3.5 (info) | 3.3.6 (info) | 3.3.7 (info) | 3.3.8 (info) | 3.3.9 | 3.3.10 | 3.3.11 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| HID (`Mouse`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Serial (`Serial`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| MIDI (`MIDI`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Storage (`MSC`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Audio (`AudioSpeaker`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Vendor (`USBVendor`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Network (`UsbNetwork`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Composite (`CompositeHidCdcMsc`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

## esp32p4

| Feature (example) | 3.3.0 (info) | 3.3.1 (info) | 3.3.2 (info) | 3.3.3 (info) | 3.3.4 (info) | 3.3.5 (info) | 3.3.6 (info) | 3.3.7 (info) | 3.3.8 (info) | 3.3.9 | 3.3.10 | 3.3.11 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| HID (`Mouse`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Serial (`Serial`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| MIDI (`MIDI`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Storage (`MSC`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Audio (`AudioSpeaker`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Vendor (`USBVendor`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Network (`UsbNetwork`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Composite (`CompositeHidCdcMsc`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

