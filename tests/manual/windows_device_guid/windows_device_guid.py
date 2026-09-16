#!/usr/bin/env python3
"""Read back what Windows recorded for the device, GUID included.

`windows_winusb.py` answers "did a driver bind"; this one answers "which GUID is
in the registry", which is the part `DeviceInterfaceGUIDs` and the vendor
revision decide. Windows stores it under the device instance's Device
Parameters, so this reads it there rather than from the descriptors - the
descriptors are what we sent, the registry is what Windows kept.

Run from `tests` on a WSL host whose Windows side can see the device, i.e. the
port must NOT be attached to WSL.

    uv run python manual/windows_device_guid/windows_device_guid.py
"""

from __future__ import annotations

import argparse
import subprocess
import sys

# No "&" in the default: it survives neither powershell.exe -Command nor the
# shell in between, and an unmatched filter looks exactly like an absent device.
DEFAULT_INSTANCE = "VID_303A*PID_4080"


def powershell(script: str) -> str:
    result = subprocess.run(
        ["powershell.exe", "-NoProfile", "-Command", script],
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--instance", default=DEFAULT_INSTANCE)
    arguments = parser.parse_args()
    pattern = "*" + arguments.instance.replace("\\", "\\\\") + "*"

    out = powershell(
        "Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like '"
        + pattern
        + "' } | ForEach-Object { "
        "'INSTANCE ' + $_.InstanceId; "
        "'STATUS   ' + $_.Status + ' problem=' + $_.Problem; "
        "$s = (Get-PnpDeviceProperty -InstanceId $_.InstanceId "
        "-KeyName 'DEVPKEY_Device_Service' -ErrorAction SilentlyContinue).Data; "
        "'SERVICE  ' + $s; "
        # Device Parameters is where the MS OS 2.0 registry property lands.
        "$p = 'HKLM:\\SYSTEM\\CurrentControlSet\\Enum\\' + $_.InstanceId + "
        "'\\Device Parameters'; "
        "$g = (Get-ItemProperty -Path $p -Name DeviceInterfaceGUIDs "
        "-ErrorAction SilentlyContinue).DeviceInterfaceGUIDs; "
        "if ($g) { $g | ForEach-Object { 'GUID     ' + $_ } } "
        "else { 'GUID     (none recorded)' } }"
    )
    if not out:
        print(
            f"No present device matching {arguments.instance}.\n"
            "If it is attached to WSL, detach it first: usbipd.exe detach --busid <n>"
        )
        return 1
    print(out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
