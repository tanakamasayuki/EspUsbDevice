# Tests

> 日本語: [README.ja.md](README.ja.md)

This directory holds the hardware tests of EspUsbDevice (pytest-embedded-arduino-cli).
What has and has not been verified is in [TEST_PLAN.md](TEST_PLAN.md).

## Requirements

- `uv`
- Arduino CLI
- The ESP32 board package (Arduino-ESP32 3.3.12, pinned in each `sketch.yaml`)
- A wired bench ([bench](#bench))

## Layout

| Directory | Contents |
| --- | --- |
| `peer/` | Two boards: the Host (EspUsbHost) and the Device (EspUsbDevice) run on separate boards. The same probes run at full speed on S3 / S2 and at high speed on a P4 / P4X direct link. [peer/README.md](peer/README.md) |
| `loopback/` | One ESP32-P4 with its HS and FS ports wired together, one controller as Host and the other as Device. [loopback/README.md](loopback/README.md) |
| `single/` | One board on its own. [single/README.md](single/README.md) |
| `pc/` | A Linux PC as Host; the PC side builds raw transfers with pyusb. [pc/README.md](pc/README.md) |
| `build_probes/` | Build only: checks that unused features are not linked. Not run by pytest. [build_probes/README.md](build_probes/README.md) |
| `sketch_support/` | Headers shared by several probes |

- Each probe is one directory holding the Host sketch, its `test_*.py` and the Device sketch in `peer_device/`.
- `examples/` is not used here. Examples are only build-checked; pass/fail on hardware comes from the probes.

## Running

Run from this directory.

```sh
uv sync --locked
cp .env.example .env   # fill in each profile's serial port
uv run --locked --env-file .env pytest --combo all --collect-only -q
uv run --locked --env-file .env pytest --combo all -q -rs --junitxml=.pytest-results/all.xml
```

| To | Command |
| --- | --- |
| Run one combo | `pytest --combo s3 -q -rs` |
| Run one probe | `pytest peer/vendor_link --combo p4_p4x_hs -q -rs` |
| Stop at the first failure | `pytest --combo all --maxfail=1 -q -rs` |
| Build only | `pytest --combo all --run-mode=build -q` |

(Prefix each with `uv run --locked --env-file .env`.)

- Run one hardware pytest at a time; several combos share boards.
- The count changes as tests are added. Check the collected count and zero errors with `--collect-only` every time.
- By default the run continues after a failure. Each probe is flashed again at the start of its module, so a failure's state is not carried over.
- When investigating one failure, add `--maxfail=1`, so a board left in a bad state does not pile up failures with other causes.

### Offline checks

```sh
python3 tools/verify_usb_backend.py   # from the repository root
```

It checks that the maintained DWC2 files are present and that no old vendor path, configuration header or `tusb*.h` include remains.
CI (`.github/workflows/tests.yml`) runs it together with `pytest --combo all --collect-only`.

### Reading the results

- Artifacts go to `.pytest-results/` (ignored by git).
- An exit status of 0 alone is not a pass. Check the collected count, the JUnit passed / skipped / failed / error counts, the number left unfinished, and that every serial log stopped cleanly (no panic).
- A skip means "not verified", not a pass.
- The raw serial logs are in the plugin's output directory (`/tmp/pytest-embedded/`). Find the one matching the JUnit timestamp, not simply the newest.

## Principles

- No conftest and no pytest plugin of our own. `--noconftest` keeps any conftest from loading; only the combos, `dut` and `peers` of the standard plugin [pytest-embedded-arduino-cli](https://github.com/tanakamasayuki/pytest-embedded-arduino-cli) (1.10.0 or later) are used.
- Each test states its start, traffic and stop in its own body.
- Wiring is expressed as plugin combos, not as a product of profiles or a custom skip hook.
- Communication failures are not hidden by retries or skips.
- When the plugin lacks something, the request goes to the plugin with a reproduction and acceptance criteria; it is not patched over in a conftest.

## Combos

`arduino_combos` in `pyproject.toml` lists only pairs that are actually wired. `--combo all` runs each combo for which a sketch has matching profiles.

| Combo | Primary (Host side) | Peer device | Link |
| --- | --- | --- | --- |
| `s3` | `s3_host` | `s3_device` | FS |
| `s3_s2` | `s3b_host` (S3) | `s2_device` (S2) | FS |
| `s2_s3` | `s2_host` (S2) | `s3b_device` (S3) | FS |
| `p4` | `p4_loopback` | none | FS (one P4's HS and FS ports) |
| `p4_psram` | `p4_psram` | none | FS (the `p4` board with PSRAM-enabled firmware) |
| `p4_p4x_hs` | `p4_hs_host` (P4 v1.x) | `p4x_hs_device` (P4X) | HS |
| `p4x_p4_hs` | `p4x_hs_host` (P4X) | `p4_hs_device` (P4 v1.x) | HS |
| `p4x_pc` | `p4x_hs_pc` (P4X) | none (the Linux PC is Host) | HS |

- A combo without matching profiles is not expanded. A test that fits no combo is skipped as `combo=none`.
- A missing port, or one board given two roles, is skipped before the build.
- Failing to open a configured port, or a failed build or upload, is an error; a communication mismatch is a failure.
- Wiring that is not permanent runs as an unnamed combo (for example `--combo primary=s2_pc`; see [pc/README.md](pc/README.md)).
- The product `--profiles=all --peer-profiles=all` is not used: it creates pairs that are not wired.

### Profiles

- Profiles live in `sketch.yaml`. The Device side refers to the library with `dir:`; the Host side uses the released EspUsbHost at a pinned version (`EspUsbHost (2.10.1)`).
- A profile name is "SoC + role" (for example `p4x_hs_device`). Another board of the same SoC is told apart as in `s3b_`. MAC addresses and tty numbers are never part of a name.
- Some probes carry compile-only profiles such as `p4_device` or `p4x_*_compile`. They are in no combo, so pytest never runs them.

## `.env`

Each board has a serial port for flashing and logs, separate from the USB link under test; `.env` assigns those ports to profiles. `.env.example` lists every key.

| Key | Board |
| --- | --- |
| `TEST_SERIAL_PORT_S3_HOST` / `TEST_SERIAL_PORT_PEER_DEVICE_S3_DEVICE` | The two S3 boards |
| `TEST_SERIAL_PORT_S3B_HOST` / `TEST_SERIAL_PORT_PEER_DEVICE_S3B_DEVICE` | The S3 paired with the S2 |
| `TEST_SERIAL_PORT_S2_HOST` / `TEST_SERIAL_PORT_PEER_DEVICE_S2_DEVICE` | The S2 paired with the S3 |
| `TEST_SERIAL_PORT_P4_LOOPBACK` / `TEST_SERIAL_PORT_P4_PSRAM` | The loopback P4 v1.x (one board) |
| `TEST_SERIAL_PORT_P4_HS_HOST` / `TEST_SERIAL_PORT_PEER_DEVICE_P4_HS_DEVICE` | The P4 v1.x of the HS link |
| `TEST_SERIAL_PORT_P4X_HS_HOST` / `TEST_SERIAL_PORT_PEER_DEVICE_P4X_HS_DEVICE` | The P4X of the HS link |
| `TEST_SERIAL_PORT_P4X_HS_PC` | The P4X on the Linux PC |
| `TEST_SERIAL_PORT_S2_PC` | An S2 on the Linux PC (not permanent; only when wired) |

- A primary uses `TEST_SERIAL_PORT_<PROFILE>`, a peer device `TEST_SERIAL_PORT_PEER_DEVICE_<PROFILE>`.
- Use stable paths such as `/run/board-identify/by-id/...`.
- One physical board may be assigned to several profiles only across different combos.

## Bench

| Position | Chip | PSRAM | Control / log | Wiring | Combo |
| --- | --- | --- | --- | --- | --- |
| Two S3 | ESP32-S3 | — | Each board's flashing port | Native OTG ports linked directly at FS | `s3` |
| S2–S3 | ESP32-S2 and ESP32-S3 | — | Each board's CH340 UART | Native USB ports linked directly at FS | `s3_s2`, `s2_s3` |
| 2-1 | ESP32-P4 v1.3 | 32 MiB | CH343 UART | The board's HS and FS ports wired together (loopback) | `p4`, `p4_psram` |
| 2-3 | ESP32-P4 v1.3 | 32 MiB | CH340 UART | HS port linked directly to 2-4 | `p4_p4x_hs`, `p4x_p4_hs` |
| 2-4 | ESP32-P4 v3.2 (P4X) | 32 MiB | USB Serial/JTAG | HS link with 2-3 | `p4_p4x_hs`, `p4x_p4_hs` |
| 3-3 / 3-4 | ESP32-P4 v3.2 (P4X) | 32 MiB | USB Serial/JTAG on 3-3 | HS port on 3-4 linked directly to the Linux PC | `p4x_pc` |

Position `N-M` is port M of hub N. The bench ledger (`oep-bench/hosts/native-linux/` README and `inventory.json`) is the authoritative record.

- Flashing and log ports are separate from the USB data link under test; test data never travels over the log path.
- Combos that share a board (`p4` and `p4_psram`, the two HS directions) never run at the same time.
- Power from 2-3's HS port reaches 2-4. Switch both ports off together.
- The plugin takes the device locks. The bench is shared with other projects, so boards are never used from outside the plugin.
- When adding a new pair, run `peer/pad_link` before building every probe to confirm that the two native ports are electrically joined. Seeing the flashing ports does not mean the USB link under test is connected.

## Adding a probe

- Use the standard layout of a primary Host and a `peer_device/` Device; the test function takes `dut` and `peers["device"]`.
- Name a combo Host first.
- Derive full-speed assumptions (MPS 64, the FS FIFO, ISO intervals, endpoint counts) from the speed and capabilities: `PROBE_BULK_MPS` from `sketch_support/probe_link.h` in a sketch, `"_hs_" in request.node.name` in a test.
- A high-speed test that falls back to full speed fails.
- NCM does not pass on lwIP traffic inside one MCU; prove the wire with raw frames and USB byte counts.
- DFU is checked with the RAM backend. Real OTA and SD writes are a separate scope.
- Add a one-line purpose to the probe table in the README of the directory it goes in.
