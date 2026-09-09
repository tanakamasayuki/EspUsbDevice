# Tests

> 日本語版: [README.ja.md](README.ja.md)

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

## Sharing the rig

The boards are shared with other projects and other Claude sessions. Tell them
before you take the hardware and tell them when you are done. Everything below
was hit for real on 2026-09-08; each one looked like a genuine defect and every
one of them was environmental.

- **Run one pytest process against the rig at a time**, even across different
  boards. Running peer and loopback at once to save time produced two failures
  that looked real - an `arduino-cli upload` error and a `usb_ncm` 90 s timeout
  whose own log showed healthy throughput to the last tick. Both passed when the
  suites were re-run alone and in sequence.
- **A P4 port lock fails the upload outright rather than queuing.** On
  `Could not exclusively lock port ... Resource temporarily unavailable`, re-run
  that test alone before drawing a conclusion. The plugin's device lock only
  coordinates pytest processes, so another project's raw esptool holding the
  port looks like this. **An upload retry was proposed and declined upstream**:
  if the other holder is mid-flash a 1-2 MB P4 image locks the port for 15-40 s,
  so a few seconds of backoff only delays the same failure, and a serial monitor
  never frees it at all. If sharing becomes routine the real fix is to run that
  project's esptool under the same device lock - a portalocker file lock in the
  plugin's lock directory, keyed by the resolved port path.
- **A compile killed by a neighbour reports `returncode=-15` with no compiler
  diagnostic.** A real failure always names a file and a line. `examples_compile`
  detects this and says "killed by signal, re-run before investigating".
- **Build-only load cannot turn a pass into a fail, but it can turn a timing
  expect into a false failure.** Hold heavy builds while someone else is
  measuring timing, and say so rather than letting them guess.
- **Do not read "the run moved past test X" as "X passed".** pytest continues to
  the next parameter after a failure. Read the result, not the position.

`pytest --clean` with no arguments collects examples_compile, loopback, peer,
then unit, so a full run stays off the boards for its first half hour or so.

After each test, the host `dut.log` and peer `peer-*.log` files are audited
automatically. Suspicious ESP-IDF errors, `ESP_ERR_*` values, panics, asserts,
and watchdog messages are summarized under `serial log audit` without failing
the test. When the HTML report is enabled, findings are also appended to that
test's expandable log. Complete serial logs remain available under
`/tmp/pytest-embedded/`.

See [TEST_PLAN.md](TEST_PLAN.md) for current coverage and planned additions.
