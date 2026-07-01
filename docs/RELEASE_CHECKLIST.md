# Release Checklist

Use this checklist before releasing `EspUsbDevice`. GitHub Actions and the
`tools/` bump scripts are shared across projects and should not be edited as part
of a normal release.

## Preflight

- `README.ja.md` / `README.md` release scope matches the implementation.
- `examples/README.ja.md` / `examples/README.md` and per-example READMEs match
  the implemented APIs.
- `docs/DEVELOPMENT_PLAN.ja.md` describes the current policy and remaining work,
  not a chronological work log.
- `docs/EXAMPLES_COVERAGE.ja.md` explains the differences from Arduino-ESP32
  bundled USB examples.
- `tests/TEST_PLAN.ja.md` / `tests/TEST_PLAN.md` state that default profiles use
  the released `EspUsbHost`.
- `TODO.ja.md` lists next-phase candidates and does not leave completed MVP work
  as open.

## Metadata

- `library.properties` `name`, `sentence`, `paragraph`, `architectures`, and
  `includes` match the public package.
- `keywords.txt` includes the main classes, methods, and constants.
- Do not hand-edit `CHANGELOG.md` or `src/espusbdevice_version.h`; let the shared
  bump script update them.

## Tests

Use `--clean` after library upgrades or profile switches to avoid stale build
cache.

```sh
cd tests
uv run --env-file .env pytest --clean
```

Optional focused checks:

```sh
uv run --env-file .env pytest examples_compile/ --clean -vv
uv run --env-file .env pytest peer/ --profile=s3_peer_host --clean -vv
uv run --env-file .env pytest loopback/ --profile=p4_loopback --clean -vv
```

## Manual Checks

These are not required for the first release, but can be run from
`tests/manual/README.md` when needed.

- `examples/MSCFatRamDisk`: PC mount, file copy, OS eject, device-side file read.
- `examples/MSCSdCard`: SD card mount, host OS read/write, OS eject.
- `examples/USBVendor`: browser / libusb / WinUSB claim, bulk echo, control
  request, WebUSB URL.

## Release

- Run the bump script to update version and changelog files.
- After the bump, verify `library.properties`, `src/espusbdevice_version.h`, and
  `CHANGELOG.md` are consistent.
- Check the final diff for unintended build artifacts, cache files, or
  local-profile-only changes.
- Create the tag and GitHub release.
