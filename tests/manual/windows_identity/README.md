# windows_identity

> 日本語版: [README.ja.md](README.ja.md)

**What does each identity field of `EspUsbDeviceConfig` do to a Windows PC
that has already seen the device - and which of them make it a *new* device?**

The user guide's [1.8](../../../docs/usb-device-guide.md#18-vid-pid-and-the-other-identity-fields)
and [5.2](../../../docs/usb-device-guide.md#52-windows) are built from the
readings below. This directory holds the sketch that produced them and the
tool that read them back, so they can be repeated on another Windows build -
which they should be: Windows changes, and every number here is one PC on one
day (Windows 11 25H2, build 26200.9457, 2026-09-16).

## What it needs

- An ESP32-S3 whose native USB goes to a Windows PC, **not attached to WSL**
  (`usbipd.exe detach --busid <n>`) - attached, it is a USBIP device to Windows
  and nothing binds
- A separate UART for flashing. **If opening that UART resets the board**, as
  it does on the rig this was measured on, read Windows *before* opening it:
  the reset is a re-plug and starts a new enumeration

## Running it

Every field is a `build_opt.h` switch; the sketch prints what it built and
what the descriptors say. Functions go in `VAR_F1`..`VAR_F3` in registration
order (1 vendor, 2 CDC, 3 HID keyboard, 4 mass storage; 0 none).

```sh
cd tests/manual/windows_identity
printf -- '-DVAR_F1=1\n' > build_opt.h                 # vendor only, PID 0x4090
arduino-cli compile --profile esp32s3 --clean . && arduino-cli upload --profile esp32s3 --port /dev/ttyACM3 .
cd ../../ && uv run python manual/windows_identity/windows_identity.py \
    --instance "VID_303A*PID_4090" --guid "{D4D4D4D4-4444-4444-8444-444444444444}" --since 11:33:30
```

Then change one thing and repeat. `--since` (local time, today) selects the
Kernel-PnP events to show; pass the time you flashed. **`--clean` every
time**: `build_opt.h` reaches the sketch through a response file but a stale
build leaves the library untouched. A value with spaces needs the whole option
quoted for the response file: `'-DVAR_PRODUCT="Identity test B"'`.

The tool prints, for every present node matching the instance pattern: the
driver Windows bound, the friendly name, the bus-reported description (what the
device sent this time), the hardware and compatible IDs, the INF, the arrival
time, the COM port name and the recorded `DeviceInterfaceGUIDs`; then every
device interface registered for the GUID with its state (`pnputil
/enum-interfaces`), what `SetupDiGetClassDevs(DIGCF_PRESENT |
DIGCF_DEVICEINTERFACE)` actually returns, and the
`Microsoft-Windows-Kernel-PnP/Configuration` events since the given time. The
last part is the one to trust when a device looks healthy and does not work:
`Get-PnpDevice` reported `STATUS OK` for a child whose start was pending and
for one whose start had already failed.

## What was measured

Identity fixed at `303a:4090`, serial `ident-1`, vendor only, one field per
row, each row read after the flash that changed it. Driver starts happened
within 10-50 ms of arrival in every working row.

| Row | Changed | Result |
|---|---|---|
| I1 | baseline | `WINUSB` on the device node, friendly name and bus description `Identity test A`, hardware IDs `USB\VID_303A&PID_4090&REV_0100`, GUID recorded, interface enabled and enumerable |
| I2 | `product` -> `Identity test B` | bus-reported description `Identity test B`; **friendly name still `Identity test A`**; no reconfiguration event |
| I3 | `manufacturer` -> `Acme Devices` | **no PnP property carries it** (Manufacturer column: `WinUsb Device`, from the INF) |
| I4 | `deviceVersion` -> `0x0200` | hardware ID `REV_0200`; same instance and driver; no reconfiguration event |
| I5 | `vid:pid` -> `1209:0001` | **new instance** `USB\VID_1209&PID_0001\IDENT-1`, driver installed fresh (400/410), GUID recorded fresh |
| I6 | `webusbEnabled` on | nothing changed on the Windows side |
| I7 | `msOs20Layout` -> Subsets | **no driver**: compatible IDs lose `USB\MS_COMP_WINUSB`, `STATUS Error problem=CM_PROB_FAILED_INSTALL`, reconfigured with `Device Updated: true`. Windows applies function subsets only to composite devices |
| I8 | `maxPowerMilliamps` 500, `selfPowered` | nothing in any PnP property; the instance recovered from I7 (`WINUSB` again, GUID recorded again) |

Then the same identity carried a CDC function through five shapes, the vendor
class registered first where present:

| Row | Functions in registration order | CDC child | COM | Other |
|---|---|---|---|---|
| C1 | CDC | `MI_00` | **COM16** | parent re-bound `WINUSB` -> `usbccgp`; `usbser` on the child; bus description `Console` (the function name) |
| C2 | CDC, vendor | `MI_00`, same instance | COM16 kept | new child `MI_02` `WINUSB`, GUID recorded, enumerable |
| C3 | vendor, CDC | `MI_01`, **new instance** | **COM34** | `MI_00` re-bound `usbser` -> `WINUSB`, still holding `PortName COM16` (inert) |
| C4 | HID, CDC | `MI_01`, same as C3 | COM34 kept | `MI_00` re-bound `WINUSB` -> `HidUsb`, `kbdhid` child started |
| C5 | CDC | `MI_00` | **COM16 again** | |
| C6 | CDC, no function name | `MI_00` | COM16 | bus description falls back to `Identity test A` (the product string) |

The parent kept the GUID from its single-interface days through all of C1-C6,
and `MI_00` kept `PortName COM16` while it was a WinUSB or HID interface.
Neither was reachable: the GUID enumeration returned exactly the live vendor
interface every time, and the COM port existed only where `usbser` was bound.

## Reading the results

- **Three fields make a new device**: `vid`, `pid`, `serialNumber`. Everything
  else changes the same instance in place.
- **Drivers and descriptors are re-read every plug-in**; the Microsoft OS 2.0
  registry property is the one thing cached behind the vendor revision, and the
  library derives that revision from the set, so it moves by itself.
- **Names are set at driver install.** The bus-reported description is live; the
  Device Manager name is whatever Windows had when it installed the driver, and
  changes only when it reconfigures the device.
- **Child instances, and therefore COM ports, are keyed by interface number.**
  Add functions after a serial port and its COM number stays; move the serial
  port and it gets a new one.
- **`msOs20Layout` stays on Auto.** Forcing subsets on a single interface leaves
  the device without a driver.
