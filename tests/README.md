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
- ESP32-P4 board for `loopback/`

## Layout

- `unit/`: no board at all. Plain Python, or the shipped C++ extracted from
  `src/` and compiled with the system g++. About five seconds, and CI runs it on
  every push without `.env` (`.github/workflows/unit-tests.yml`).
- `single/`: one device board, no USB host board. Uploads a sketch that calls
  the real API on the real chip and prints `OK` / `NG`. Needs `.env`.
- `peer/`: two-board tests using EspUsbHost as host and EspUsbDevice as device.
- `loopback/`: one-board ESP32-P4 tests running EspUsbHost and EspUsbDevice together.
- `manual/`: tests that require physical devices or visual confirmation.

## Run Mode

From this directory:

```sh
uv run --env-file .env pytest
uv run --env-file .env pytest peer/
uv run --env-file .env pytest --run-mode=build

# The unit layer needs no board and no ports, so it needs no .env.
uv run pytest unit/
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

## The shape of a peer test

Every module under `peer/` is one pytest test. The cases inside it are ordinary
named functions, driven from a list:

```python
def _enumeration(dut, device): ...
def _keyboard(dut, device): ...


def test_composite_hid_cdc(dut, peers):
    device = peers["device"]

    device.write("?")
    device.expect_exact("DEVICE_READY 1")

    for check in (_enumeration, _keyboard):
        check(dut, device)
```

Two reasons. A failure names the function it happened in, not just a line
number; and a module that is one test cannot have tests that only pass in a
particular order.

### Ask, do not await

Both sketches answer questions instead of announcing facts at boot.

- The device answers `?` with `DEVICE_READY <0|1>`, and blocks first: its command
  handler calls `waitForHost()`, which spins on `device.ready()` - `tud_mounted()`,
  the host having completed SET_CONFIGURATION. Module-specific state follows on
  its own line (`DEVICE_NET`, `DEVICE_CABLES`, `DEVICE_NKRO`, ...), never appended
  to the `DEVICE_READY` line.
- The host blocks the same way, in `waitForDevice()`, on the address its
  `onDeviceConnected` latched. Where nothing else it prints reports the
  connection - the sketches that only forward callback output - it also answers
  `?` with `HOST_READY <0|1> vid=.... pid=....`.
- Anything the host learns once, at enumeration, is kept as well as printed, and
  can be asked for again: `D` replays a HID report descriptor summary, `S`
  replays the audio stream report.

This is what makes position irrelevant. A line printed once at boot is only
visible to whichever test reads it first; a question can be asked at any point,
and asking it asserts the same thing waiting for the banner did.

`peer/usb_msc` has had this shape from the start and was the only peer module
that survived being run in reverse while the rest were still reading banners.

### Modules that are deliberately ordered

Four modules keep an ordered case list on purpose, and say so in their
docstring:

- `usb_serial` - the last line-coding step asserts that a partial SET_LINE_CODING
  left the earlier fields alone, which is only a claim after the earlier steps.
- `usb_serial_multi` - the separation check reads counters the two per-port
  exchanges produce.
- `usb_midi_cables` - the first and last cases are a before/after pair around
  every message the module sends.
- `usb_vendor` - the first case asserts an exact device RX count, which only
  holds while the counters are the ones the session started with.

Everything else should pass with its case list reversed: wrap the tuple in
`reversed(...)` and re-run that module. It is one pytest invocation and one
upload, so the check costs a minute rather than a flash cycle per case, and it is
worth running on any module whose cases have just been rewritten.

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
  diagnostic.** A real failure always names a file and a line. `tools/build_check.py`
  detects this and says "killed by signal, re-run before investigating".
- **Build-only load cannot turn a pass into a fail, but it can turn a timing
  expect into a false failure.** Hold heavy builds while someone else is
  measuring timing, and say so rather than letting them guess.
- **Do not read "the run moved past test X" as "X passed".** pytest continues to
  the next parameter after a failure. Read the result, not the position.

`pytest --clean` with no arguments collects loopback, peer, single, then unit. Building
the examples is not part of that run - `tools/build_check.py` and the CI Build
Check workflow cover it.

After each test, the host `dut.log` and peer `peer-*.log` files are audited
automatically. Suspicious ESP-IDF errors, `ESP_ERR_*` values, panics, asserts,
and watchdog messages are summarized under `serial log audit` without failing
the test. When the HTML report is enabled, findings are also appended to that
test's expandable log. Complete serial logs remain available under
`/tmp/pytest-embedded/`.

See [TEST_PLAN.md](TEST_PLAN.md) for current coverage and planned additions.
