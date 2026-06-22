# Tests

> 日本語: [README.ja.md](README.ja.md)

This directory contains the EspUsbDevice test specifications and, later, the
automated pytest-embedded tests.

The structure intentionally mirrors EspUsbHost so peer tests can move from
Arduino-ESP32 USB device sketches to EspUsbDevice sketches incrementally.

## Requirements

- `uv`
- Arduino CLI
- ESP32 board packages for the target boards
- ESP32-S3 boards for `peer/`
- ESP32-P4 board for `loopback/` and `probe/`

## Layout

- `unit/`: host-independent descriptor and report helper tests.
- `peer/`: two-board tests using EspUsbHost as host and EspUsbDevice as device.
- `loopback/`: one-board ESP32-P4 host/device tests.
- `probe/`: bring-up sketches for P4 port and speed investigation.
- `manual/`: tests that require physical devices or visual confirmation.

## Run Mode

From this directory:

```sh
uv run --env-file .env pytest
uv run --env-file .env pytest peer/
uv run --env-file .env pytest --run-mode=build
```

The exact tests will be added as the library implementation lands.
