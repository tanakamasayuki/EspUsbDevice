"""Consumer Control usages (media keys), across two boards.

The preconditions are asked rather than awaited: the device answers '?' once the
host has configured it, and the host answers '?' once it has enumerated the peer.
The boot banners this used to read are printed once, so they only ever worked for
a test that ran first.

The usages are driven from a list. Each click is pressed and released before the
next one, so the order is not load-bearing.
"""

# Usage IDs from HID Usage Table 12 (Consumer Page). Checked as numbers rather
# than names because it is the number that goes on the wire.
USAGES = [
    ("u", 0x00E9),  # Volume Up
    ("d", 0x00EA),  # Volume Down
    ("p", 0x00CD),  # Play/Pause
    ("n", 0x00B5),  # Next Track
    ("s", 0x00B6),  # Previous Track
    ("m", 0x00E2),  # Mute
]


def expect_consumer_click(dut, device, command, usage):
    device.write(command)
    device.expect_exact(f"CMD {command} 1")
    dut.expect_exact(f"CONSUMER usage=0x{usage:04x} pressed=1 released=0")
    dut.expect_exact(f"CONSUMER usage=0x{usage:04x} pressed=0 released=1")


def test_hid_consumer_control(dut, peers):
    device = peers["device"]

    device.write("?")
    device.expect_exact("DEVICE_READY 1")
    dut.write("?")
    dut.expect_exact("HOST_READY 1 vid=303a pid=4006")

    for command, usage in USAGES:
        expect_consumer_click(dut, device, command, usage)
