# Loopback Tests

> 日本語: [README.ja.md](README.ja.md)

`tests/loopback` contains one-board ESP32-P4 tests where EspUsbHost and
EspUsbDevice run on the same chip.

The first target is HID keyboard loopback with descriptor logging so P4
port/speed behavior can be verified before broader class coverage is added.

## Matrix

| Device | Host | Expected |
|--------|------|----------|
| FS device | FS host | Primary target if the SDK allows this pairing. |
| HS device | HS host | Expected baseline for P4 high-speed paths. |
| FS device | HS host | Probe/diagnostic case. |
| HS device | FS host | Expected to fail or be unsupported; document explicitly. |
