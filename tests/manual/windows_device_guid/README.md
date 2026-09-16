# windows_device_guid

> 日本語版: [README.ja.md](README.ja.md)

**Does Windows record the GUID we asked for, and does changing it take effect on
a PC that has already seen the device?**

The second question is the one that matters. Windows caches the registry
properties it read the first time it enumerated a VID/PID/serial and re-reads
them only when `MS_OS_20_FEATURE_VENDOR_REVISION` changes. A library that never
emits that descriptor can publish a new `DeviceInterfaceGUIDs` all it likes and
no PC that has met the device will ever see it - which is exactly what
EspUsbDevice 2.4.0 did.

Automated tests cannot answer this. `tests/single/descriptor` proves the bytes
we send are right; only Windows can say what it kept.

## What it needs

- An ESP32-S3 whose native USB goes to a Windows PC, **not attached to WSL**
  (`usbipd.exe detach --busid <n>`) - an attached device shows up on the Windows
  side as a USBIP Shared Device and Windows binds nothing to it
- A separate UART for flashing, because the native USB is the thing under test

## Running it

The identity is fixed across every variant (`303a:4080`, serial `guid-test-1`)
and only the GUID and revision move. That is the whole design: a changed
identity would make Windows read the descriptors afresh for what it considers a
new device, which proves nothing.

```sh
cd tests/manual/windows_device_guid
# A: GUID A, revision derived
rm -f build_opt.h
arduino-cli compile --profile esp32s3 --clean . && arduino-cli upload --profile esp32s3 --port /dev/ttyACM3 .
cd ../../ && uv run python manual/windows_device_guid/windows_device_guid.py
```

Then repeat with `-DGUID_VARIANT=1` (GUID B), with
`-DGUID_VARIANT=2 -DPINNED_REVISION=<B's revision>` (the control), and with
`-DGUID_VARIANT=2` alone. The sketch prints its revision at boot, which is where
the control's number comes from. **`--clean` every time**: `build_opt.h` reaches
the sketch through a response file but a stale build leaves the library
untouched.

## What to expect

Measured on Windows 11, instance `USB\VID_303A&PID_4080\GUID-TEST-1` throughout,
every variant `STATUS OK problem=CM_PROB_NONE` and `SERVICE WINUSB`:

| Variant | GUID sent | Revision | GUID Windows kept |
|---|---|---|---|
| A | `{A1A1…}` | 21192 (derived) | `{A1A1…}` |
| B | `{B2B2…}` | 563 (derived) | `{B2B2…}` - updated |
| **C, control** | `{C3C3…}` | **563, pinned** | **`{B2B2…}` - not updated** |
| C′ | `{C3C3…}` | 12898 (derived) | `{C3C3…}` - updated |

### The rest of the matrix

The same sketch covers the identity and layout axes, which is what the user
guide's "what Windows re-reads" table is built from. `-DVAR_PID=`,
`-DVAR_SERIAL=`, `-DVAR_NO_SERIAL=1` and `-DVAR_COMPOSITE=1` select them.
Measured, all `STATUS OK` with a driver bound:

| Change, revision pinned unless noted | Instance | Result |
|---|---|---|
| vendor only -> vendor + HID | same parent, new `&MI_00` / `&MI_01` | parent re-bound to `usbccgp`, children created, `MI_01` got `WINUSB` and read the GUID fresh |
| vendor + HID -> vendor only | same parent | **parent re-bound `usbccgp` -> `WINUSB`**, GUID kept (revision unchanged) |
| GUID changed on the `&MI_01` child | same child | GUID kept - the cache applies to children too |
| built against published 2.4.0, then against the fix | same | revision descriptor appears for the first time and the new GUID **is** taken |
| `pid` 0x4080 -> 0x4083 | **new** | everything read fresh, revision pinned to an unused value |
| `serialNumber` changed | **new** | everything read fresh |
| no `serialNumber` at all | `…\8&2EBC545B&0&4` | keyed on the port, not a serial |
| single interface (GUID A) -> composite (GUID B), fresh PID | parent kept, children new | **parent keeps A at device scope while the child carries B** - one device, two GUIDs, the stale one on a node that cannot serve WinUSB |
| swap the functions at `MI_00` / `MI_01`, count unchanged | **both children unchanged** | both re-bound: `HidUsb`->`WINUSB` and `WINUSB`->`USBSTOR`. The GUID was **added** to the child that gained the vendor function, and **left behind** on the one that lost it |

Two conclusions the user guide leans on: **driver binding follows the
descriptors on every enumeration and needs no revision**, and **only the
Microsoft OS 2.0 registry properties are cached behind one**.

**The control is the test.** Without it, B updating only shows that Windows
re-read something; it cannot distinguish "the revision made it re-read" from
"it re-reads every time". C sends a different GUID under an unchanged revision
and Windows keeps the old one, which is the mechanism working exactly as the
specification describes. C′ then shows the device is not stuck: derive the
revision again and the new GUID lands.
