# Tests

> 日本語: [README.ja.md](README.ja.md)

This directory contains the EspUsbDevice test specifications and automated
pytest-embedded tests.

The structure intentionally mirrors EspUsbHost so peer tests can move from
Arduino-ESP32 USB device sketches to EspUsbDevice sketches incrementally.
ESP32-P4 loopback is a primary target here because Arduino-ESP32's standard
Device implementation is fixed to high-speed behavior on P4 and is difficult to
pair with the FS host side. EspUsbDevice controls port, speed, and endpoint MPS
explicitly for those tests.

## Requirements

- `uv`
- Arduino CLI
- ESP32 board packages for the target boards
- ESP32-S3 boards for `peer/`
- ESP32-P4 board for `loopback/` and `probe/`

## Layout

- `unit/`: host-independent descriptor, report helper, and FAT RAM disk tests.
- `examples_compile/`: build-only smoke tests for examples sketches.
- `peer/`: two-board tests using EspUsbHost as host and EspUsbDevice as device.
- `loopback/`: one-board ESP32-P4 tests running EspUsbHost and EspUsbDevice together.
- `probe/`: bring-up sketches for P4 port and speed investigation.
- `manual/`: tests that require physical devices or visual confirmation.

## Run Mode

From this directory:

```sh
uv run --env-file .env pytest
uv run --env-file .env pytest peer/
uv run --env-file .env pytest --run-mode=build
uv run --env-file .env pytest examples_compile/
```

Regular peer and loopback tests use the released EspUsbHost version. Local
profiles are only for pre-release validation of unreleased Host-side fixes.

```sh
uv run --env-file .env pytest peer/ --profile=s3_peer_local
uv run --env-file .env pytest loopback/ --profile=p4_loopback_local
```

After upgrading `EspUsbHost` / `EspUsbDevice`, or after switching between release
and local profiles, stale build cache / intermediate files can cause boot-time
crashes or unexpected timeouts. Rebuild with `--clean` in that case.

```sh
uv run --env-file .env pytest peer/ --profile=s3_peer_host --clean
uv run --env-file .env pytest loopback/ --profile=p4_loopback --clean
```

After each test, the host `dut.log` and peer `peer-*.log` files are audited
automatically. Suspicious ESP-IDF errors, `ESP_ERR_*` values, panics, asserts,
and watchdog messages are summarized under `serial log audit` without failing
the test. When the HTML report is enabled, findings are also appended to that
test's expandable log. Complete serial logs remain available under
`/tmp/pytest-embedded/`.

See [TEST_PLAN.md](TEST_PLAN.md) for current coverage and planned additions.
