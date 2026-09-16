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
`-DVAR_SERIAL=`, `-DVAR_NO_SERIAL=1` and `-DVAR_COMPOSITE=1` (HID + vendor),
`=2` (vendor + MSC), `=3` (MSC registered first, then vendor) select them;
`-DVAR_HID_FIRST=1` registers the HID class before the vendor class in variant
1. Measured, all `STATUS OK` with a driver bound:

| Change, revision pinned unless noted | Instance | Result |
|---|---|---|
| vendor only -> vendor + HID | same parent, new `&MI_00` / `&MI_01` | parent re-bound to `usbccgp`, children created, `MI_01` got `WINUSB` and read the GUID fresh |
| vendor + HID -> vendor only | same parent | **parent re-bound `usbccgp` -> `WINUSB`**, GUID kept (revision unchanged) |
| GUID changed on the `&MI_01` child | same child | GUID kept - the cache applies to children too |
| built against published 2.4.0, then against the fix | same | revision descriptor appears for the first time and the new GUID **is** taken |
| `pid` 0x4080 -> 0x4083 | **new** | everything read fresh, revision pinned to an unused value |
| `serialNumber` changed | **new** | everything read fresh |
| no `serialNumber` at all | `…\8&2EBC545B&0&4` | keyed on the port, not a serial |
| single interface (GUID A) -> composite (GUID B), fresh PID | parent kept, children new | parent keeps A at device scope while the child carries B |
| swap the functions at `MI_00` / `MI_01`, count unchanged | **both children unchanged** | both re-bound: `HidUsb`->`WINUSB` and `WINUSB`->`USBSTOR`. The GUID was **added** to the child that gained the vendor function, and **left behind** on the one that lost it |
| `msOs20CcgpDevice` on a single vendor interface | parent + one child | parent binds `usbccgp`, child `&MI_00` binds `WINUSB`, and the GUID lands **only on the child** - the device-scope value is never written |
| enumerate with `SetupDiGetClassDevs(DIGCF_PRESENT \| DIGCF_DEVICEINTERFACE)`, stale value on the **parent** | - | **the stale GUID returns nothing; the live one returns exactly one interface** |
| same, stale value on a **child** (`MI_01` now `USBSTOR`, same GUID as the live `MI_00`) | - | **one interface, `MI_00` only** - the leftover on the mass-storage child is not enumerated either |

Those last two rows decide how much the leftovers matter, and both were measured
only after the earlier rows had been written up as if a stale value were an
enumerable device. It is not: a registry value does not create a device
interface, the driver bound to that node does. The parent case was measured
first, and the child case only after the requester pointed out that the same
unverified inference was still standing for it.

Two conclusions the user guide leans on: **driver binding follows the
descriptors on every enumeration and needs no revision**, and **only the
Microsoft OS 2.0 registry properties are cached behind one**.

### The row that was a library bug

Every HID + vendor build above had the vendor function at `MI_01` bound to
`WINUSB`, `STATUS OK`, GUID recorded - and **not enumerable**:
`pnputil /enum-interfaces` listed the interface as disabled and
`SetupDiGetClassDevs(DIGCF_PRESENT)` returned nothing. Vendor at `MI_00` next
to MSC enumerated fine, so it looked like an interface-number or HID-sibling
effect on Windows' side. It was neither:

| Build, fresh PID each | HID child | Vendor child | Interface |
|---|---|---|---|
| HID + vendor, vendor registered first | `HidUsb` **Code 10, ~6 s after arrival** | started 2 ms after that failure | registered, **disabled** |
| MSC + vendor, vendor at `MI_01` (variant 3) | - | started 27 ms after arrival | **enabled** |
| HID + vendor, `-DVAR_HID_FIRST=1` | started 21 ms after arrival | started 33 ms after arrival | **enabled** |
| HID + vendor, vendor registered first, **fixed library** | started 23 ms | started 36 ms | **enabled** |

Those times are from the `Microsoft-Windows-Kernel-PnP/Configuration` event
log (ids 400/410/411). It disagrees with `Get-PnpDevice`, which reported
`STATUS OK problem=CM_PROB_NONE` for both children at every check - including
one taken while the HID start was still pending and one taken a minute after
the log had recorded its Code 10. Read the event log, not the status column,
when a child looks healthy and does not work. Why the WinUSB child, once
started after the failure, still never enabled its interface was not
determined; the interface was disabled at every check from 20 s to several
minutes after arrival, and once, about nine minutes in, the port showed
"Unknown USB Device (Device Descriptor Request Failed)" instead. The fix
removes the failure those states follow from, and with it every one of them.

The cause was in the library: the HID class was looked up by TinyUSB's
instance number as if it were a position in the registration table, so a
HID class registered after the vendor class returned no report descriptor
and the host's `GET_DESCRIPTOR(Report)` was neither answered nor stalled.
The descriptors were correct throughout - which is exactly why the
descriptor dump this sketch prints did not point at it. Registration order
was the only difference between the failing and working HID rows, and it is
not visible on the wire. `tests/single/hid_registration_order` and
`tests/peer/composite_vendor_hid` guard it now.

**The control is the test.** Without it, B updating only shows that Windows
re-read something; it cannot distinguish "the revision made it re-read" from
"it re-reads every time". C sends a different GUID under an unchanged revision
and Windows keeps the old one, which is the mechanism working exactly as the
specification describes. C′ then shows the device is not stuck: derive the
revision again and the new GUID lands.
