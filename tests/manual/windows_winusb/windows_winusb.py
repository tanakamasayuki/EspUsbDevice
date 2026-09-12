#!/usr/bin/env python3
"""Ask Windows what it did with the vendor interface.

Run from ``tests`` on a WSL host whose Windows side can see the device - that
means the OTG port must NOT be attached to WSL (`usbipd detach`), because an
attached device shows up on the Windows side as a USBIP Shared Device and
Windows never binds a driver to it.

    uv run python manual/windows_winusb/windows_winusb.py
"""

from __future__ import annotations

import argparse
import subprocess
import sys

DEFAULT_INSTANCE = "USB\\VID_303A&PID_4043"


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
    pattern = arguments.instance.replace("\\", "\\\\")

    listing = powershell(
        "Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like '"
        + pattern
        + "*' } | Select-Object Status,Problem,Class,FriendlyName,InstanceId | Format-List"
    )
    if not listing:
        print(
            f"No present device matching {arguments.instance}.\n"
            "If it is attached to WSL, detach it first: usbipd.exe detach --busid <n>"
        )
        return 1
    print(listing)

    ids = powershell(
        "Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like '"
        + pattern
        + "*' } | ForEach-Object { "
        "$c = (Get-PnpDeviceProperty -InstanceId $_.InstanceId "
        "-KeyName 'DEVPKEY_Device_CompatibleIds' -ErrorAction SilentlyContinue).Data; "
        "$s = (Get-PnpDeviceProperty -InstanceId $_.InstanceId "
        "-KeyName 'DEVPKEY_Device_Service' -ErrorAction SilentlyContinue).Data; "
        "'INSTANCE ' + $_.InstanceId; 'SERVICE  ' + $s; "
        "($c | ForEach-Object { 'COMPAT   ' + $_ }) }"
    )
    print(ids)

    winusb_compatible = "MS_COMP_WINUSB" in ids.upper()
    winusb_bound = "WINUSB" in ids.upper().split("COMPAT")[0]
    print()
    print(f"USB\\MS_COMP_WINUSB present : {winusb_compatible}")
    print(f"WinUSB is the bound service: {winusb_bound}")
    print("(Status OK with no Problem, plus the compatible ID, is the pass.)")
    return 0 if winusb_compatible else 2


if __name__ == "__main__":
    raise SystemExit(main())
