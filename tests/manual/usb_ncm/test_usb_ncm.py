"""Manual test for the USB CDC-NCM network device (examples/manual usb_ncm).

Physical setup required (that is why it lives under tests/manual):
  1. Flash usb_ncm to an ESP32-S3 (the ``esp32s3`` profile; the
     ``test_usb_ncm_flash_and_enumerate`` test does this for you).
  2. Cable the board's USB-OTG port to the PC running these tests.
  3. The board runs a DHCP server on 192.168.7.0/24 and answers at 192.168.7.1.

The enumeration test needs the device's log serial to be reachable. The ping
test does not — it only needs host-side IP reachability to 192.168.7.1, so it
works from WSL (which reaches the Windows-side USB NIC by routing) even when the
device serial is not directly visible. Override the target with NCM_TEST_IP.
"""

import os
import shutil
import subprocess

import pytest

NCM_IP = os.environ.get("NCM_TEST_IP", "192.168.7.1")


def _ping(ip: str, count: int = 3, timeout_s: int = 2) -> subprocess.CompletedProcess:
    # Linux/WSL ping: -c count, -W per-reply timeout (seconds).
    return subprocess.run(
        ["ping", "-c", str(count), "-W", str(timeout_s), ip],
        capture_output=True,
        text=True,
    )


def test_usb_ncm_flash_and_enumerate(dut):
    """Flash via the esp32s3 profile and confirm the network interface comes up."""
    dut.expect_exact("NCM_BEGIN 1 ESP_OK")
    dut.expect_exact(f"NCM_NET 1 ip={NCM_IP}")


def test_usb_ncm_ping():
    """Host can reach the device over the USB network link (DHCP + lwIP + TX/RX)."""
    if shutil.which("ping") is None:
        pytest.skip("ping not available on this host")

    result = _ping(NCM_IP)
    if result.returncode != 0:
        pytest.fail(
            f"ping {NCM_IP} failed (rc={result.returncode}). Flash usb_ncm, connect "
            f"the board's USB-OTG port to this PC, and confirm the USB NIC has a "
            f"192.168.7.x address.\n--- stdout ---\n{result.stdout}\n--- stderr ---\n{result.stderr}"
        )
