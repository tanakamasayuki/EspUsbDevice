"""System Control usages (power, standby), across two boards.

The preconditions are asked rather than awaited; see
tests/peer/hid_consumer_control, which this mirrors on the Generic Desktop page.
"""

# Usage IDs from HID Usage Table 1 (Generic Desktop).
USAGES = [
    ("p", 0x01),  # System Power Down
    ("s", 0x02),  # System Sleep / standby
]


def expect_system_click(dut, device, command, usage):
    device.write(command)
    device.expect_exact(f"CMD {command} 1")
    dut.expect_exact(f"SYSTEM usage=0x{usage:02x} pressed=1 released=0")
    dut.expect_exact(f"SYSTEM usage=0x{usage:02x} pressed=0 released=1")


def test_hid_system_control(dut, peers):
    device = peers["device"]

    device.write("?")
    device.expect_exact("DEVICE_READY 1")
    dut.write("?")
    dut.expect_exact("HOST_READY 1 vid=303a pid=4007")

    for command, usage in USAGES:
        expect_system_click(dut, device, command, usage)
