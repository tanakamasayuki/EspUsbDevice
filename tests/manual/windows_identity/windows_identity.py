#!/usr/bin/env python3
"""Read back everything Windows decided about one USB identity.

This is the investigation procedure the user guide describes, as one command:
for every present PnP node whose instance ID matches, the driver Windows bound,
the names it shows, the hardware IDs it matched on, the COM port it assigned,
the DeviceInterfaceGUIDs it recorded; then the registered device interfaces
for a GUID and whether each is enabled; then what an application enumerating
that GUID would actually get; then the Kernel-PnP configuration events since a
given time, which is where "was started" and "had a problem starting" live -
`Get-PnpDevice` says `OK` for a child whose start is still pending, and even
for one whose start has already failed (measured).

Run from a WSL shell whose Windows side can see the device. The device must
NOT be attached to WSL (usbipd) - an attached device is a USBIP device to
Windows and nothing binds.

    uv run python manual/windows_identity/windows_identity.py --instance "VID_303A*PID_4090"
    uv run python manual/windows_identity/windows_identity.py --instance "VID_303A*PID_4090" \\
        --guid "{D4D4D4D4-4444-4444-8444-444444444444}" --since 11:42:00

`--since` is local time, today. `--instance` is a wildcard on the instance ID;
keep `&` out of it (it does not survive the shell / PowerShell boundary).
"""

from __future__ import annotations

import argparse
import subprocess

SCRIPT = r'''
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$pattern = '*{PATTERN}*'
$guid = '{GUID}'
$since = '{SINCE}'

function Prop($id, $key) {
  (Get-PnpDeviceProperty -InstanceId $id -KeyName $key -ErrorAction SilentlyContinue).Data
}

'--- nodes'
Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like $pattern } | Sort-Object InstanceId | ForEach-Object {
  $id = $_.InstanceId
  'INSTANCE  ' + $id
  'STATUS    ' + $_.Status + ' problem=' + $_.Problem + ' class=' + $_.Class
  'SERVICE   ' + (Prop $id 'DEVPKEY_Device_Service')
  'FRIENDLY  ' + $_.FriendlyName
  'DEVDESC   ' + (Prop $id 'DEVPKEY_Device_DeviceDesc')
  'BUSDESC   ' + (Prop $id 'DEVPKEY_Device_BusReportedDeviceDesc')
  'MANUF     ' + (Prop $id 'DEVPKEY_Device_Manufacturer')
  $hw = Prop $id 'DEVPKEY_Device_HardwareIds'
  if ($hw) { 'HWIDS     ' + ($hw -join ' | ') }
  $cid = Prop $id 'DEVPKEY_Device_CompatibleIds'
  if ($cid) { 'COMPATIDS ' + (($cid | Select-Object -First 3) -join ' | ') }
  'DRIVER    ' + (Prop $id 'DEVPKEY_Device_DriverInfPath') + ' ' + (Prop $id 'DEVPKEY_Device_DriverVersion')
  'ARRIVED   ' + (Prop $id 'DEVPKEY_Device_LastArrivalDate')
  $p = 'HKLM:\SYSTEM\CurrentControlSet\Enum\' + $id + '\Device Parameters'
  $port = (Get-ItemProperty -Path $p -Name PortName -ErrorAction SilentlyContinue).PortName
  if ($port) { 'PORTNAME  ' + $port }
  $g = (Get-ItemProperty -Path $p -Name DeviceInterfaceGUIDs -ErrorAction SilentlyContinue).DeviceInterfaceGUIDs
  if ($g) { $g | ForEach-Object { 'GUID      ' + $_ } } else { 'GUID      (none recorded)' }
  ''
}

if ($guid) {
  '--- device interfaces registered for ' + $guid + ' (pnputil /enum-interfaces: path, then state)'
  # pnputil writes in the console OEM code page; read it as that, print as UTF-8.
  $oem = [System.Text.Encoding]::GetEncoding([System.Globalization.CultureInfo]::CurrentCulture.TextInfo.OEMCodePage)
  [Console]::OutputEncoding = $oem
  $lines = & pnputil.exe /enum-interfaces /class $guid 2>&1
  [Console]::OutputEncoding = [System.Text.Encoding]::UTF8
  $block = @()
  foreach ($l in $lines) {
    if ($l -match '^\s*$') {
      if ($block.Count -ge 5) { '  ' + ($block[0] -replace '^[^:]*:\s*','') ; '      ' + ($block[$block.Count-1] -replace '^[^:]*:\s*','') }
      $block = @()
    } else { $block += $l }
  }
  if ($block.Count -ge 5) { '  ' + ($block[0] -replace '^[^:]*:\s*','') ; '      ' + ($block[$block.Count-1] -replace '^[^:]*:\s*','') }
  ''
  '--- what SetupDiGetClassDevs(DIGCF_PRESENT | DIGCF_DEVICEINTERFACE) returns for it'
  $src = @"
using System; using System.Runtime.InteropServices;
public class Ifc {
  [StructLayout(LayoutKind.Sequential)] public struct DATA { public int cbSize; public Guid g; public int Flags; public IntPtr r; }
  [DllImport("setupapi.dll", CharSet=CharSet.Unicode, SetLastError=true)] public static extern IntPtr SetupDiGetClassDevs(ref Guid g, IntPtr e, IntPtr h, int f);
  [DllImport("setupapi.dll", CharSet=CharSet.Unicode, SetLastError=true)] public static extern bool SetupDiEnumDeviceInterfaces(IntPtr s, IntPtr d, ref Guid g, int i, ref DATA a);
  [DllImport("setupapi.dll", CharSet=CharSet.Unicode, SetLastError=true)] public static extern bool SetupDiGetDeviceInterfaceDetail(IntPtr s, ref DATA a, IntPtr d, int dz, ref int req, IntPtr dd);
  [DllImport("setupapi.dll")] public static extern int SetupDiDestroyDeviceInfoList(IntPtr s);
}
"@
  Add-Type -TypeDefinition $src -ErrorAction SilentlyContinue
  $g = [Guid]$guid
  $h = [Ifc]::SetupDiGetClassDevs([ref]$g, [IntPtr]::Zero, [IntPtr]::Zero, 0x12)
  $i = 0; $found = 0
  while ($true) {
    $d = New-Object Ifc+DATA
    $d.cbSize = [System.Runtime.InteropServices.Marshal]::SizeOf($d)
    if (-not [Ifc]::SetupDiEnumDeviceInterfaces($h, [IntPtr]::Zero, [ref]$g, $i, [ref]$d)) { break }
    $req = 0
    [void][Ifc]::SetupDiGetDeviceInterfaceDetail($h, [ref]$d, [IntPtr]::Zero, 0, [ref]$req, [IntPtr]::Zero)
    $buf = [System.Runtime.InteropServices.Marshal]::AllocHGlobal($req)
    [System.Runtime.InteropServices.Marshal]::WriteInt32($buf, 8)
    if ([Ifc]::SetupDiGetDeviceInterfaceDetail($h, [ref]$d, $buf, $req, [ref]$req, [IntPtr]::Zero)) {
      '  PRESENT ' + [System.Runtime.InteropServices.Marshal]::PtrToStringUni([IntPtr]::Add($buf, 4)); $found++
    }
    [System.Runtime.InteropServices.Marshal]::FreeHGlobal($buf)
    $i++
  }
  if ($found -eq 0) { '  (none present)' }
  [void][Ifc]::SetupDiDestroyDeviceInfoList($h)
  ''
}

if ($since) {
  '--- Kernel-PnP configuration events since ' + $since + ' (400 configured, 410 started, 411 start failed, 420 deleted)'
  $t = [datetime]::Today.Add([timespan]::Parse($since))
  Get-WinEvent -LogName 'Microsoft-Windows-Kernel-PnP/Configuration' -MaxEvents 400 -ErrorAction SilentlyContinue |
    Where-Object { $_.TimeCreated -ge $t -and $_.Message -like $pattern -and $_.Id -ne 442 } |
    Sort-Object TimeCreated | ForEach-Object {
      $m = ($_.Message -split "`r?`n")[0]
      $extra = ''
      if ($_.Message -match 'Service: (\S+)') { $extra += ' [' + $matches[1] + ']' }
      if ($_.Message -match 'Problem: (\S+)') { $extra += ' problem=' + $matches[1] }
      if ($_.Message -match 'Problem Status: (\S+)') { $extra += ' status=' + $matches[1] }
      if ($_.Message -match 'Device Updated: (\S+)') { $extra += ' updated=' + $matches[1] }
      '  ' + $_.TimeCreated.ToString('HH:mm:ss.fff') + ' id=' + $_.Id + ' ' + $m + $extra
    }
}
'''


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--instance", default="VID_303A*PID_4090", help="wildcard on the instance ID, no '&'")
    parser.add_argument("--guid", default="", help="device interface GUID to report registrations and enumeration for")
    parser.add_argument("--since", default="", help="HH:MM:SS local time; list Kernel-PnP events from then on")
    arguments = parser.parse_args()
    script = (
        SCRIPT.replace("{PATTERN}", arguments.instance)
        .replace("{GUID}", arguments.guid)
        .replace("{SINCE}", arguments.since)
    )
    result = subprocess.run(
        ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", script],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    out = result.stdout.replace("\r", "")
    print(out.strip())
    if result.returncode != 0 and result.stderr.strip():
        print(result.stderr.strip()[:2000])
        return 1
    if "INSTANCE" not in out:
        print(f"No present device matching {arguments.instance}. Attached to WSL? usbipd.exe detach --busid <n>")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
