"""Gamepad reports: axes, hat switch, buttons, across two boards.

One test rather than three. The first read `DEVICE_BEGIN` and `HOST_CONNECTED`,
both printed once at boot, and the other two inherited the enumeration it had
waited for.

The host sketch is entirely event-driven - it prints what the gamepad callback
gives it - so it had no way to be asked anything. It has one command now, '?',
which answers with the peer's identity once the peer is enumerated. The device
blocks on `device.ready()` before handling any command, so a report is only ever
sent to a configured host.

The cases are named functions driven from a list. Each returns the gamepad to
neutral before it finishes, so the order is not load-bearing.
"""


def expect_gamepad_report(dut, device, command, report):
    device.write(command)
    device.expect_exact(f"CMD {command} 1")
    dut.expect(f"GAMEPAD report={report} fields=[1-9][0-9]*")


NEUTRAL = "00 00 00 00 00 00 00 00 00 00 00"


def _axes(dut, device):
    """Six signed axes at once, then back to neutral: the report layout has to
    place each axis in its own byte, which a single-axis check would not show."""
    expect_gamepad_report(dut, device, "a", "0a f6 14 ec 1e e2 03 05 00 00 00")
    expect_gamepad_report(dut, device, "0", NEUTRAL)


def _hat(dut, device):
    """All eight hat directions, each followed by centre, so a direction that
    fails to clear is visible as the next one arriving wrong."""
    for command, hat in [("1", 1), ("2", 2), ("3", 3), ("4", 4), ("5", 5), ("6", 6), ("7", 7), ("8", 8)]:
        expect_gamepad_report(dut, device, command, f"00 00 00 00 00 00 {hat:02x} 00 00 00 00")
        expect_gamepad_report(dut, device, "0", NEUTRAL)


def _buttons(dut, device):
    """Fifteen buttons held together, which is the width of the button field."""
    expect_gamepad_report(dut, device, "b", "00 00 00 00 00 00 00 ff 7f 00 00")
    expect_gamepad_report(dut, device, "0", NEUTRAL)


def test_hid_gamepad(dut, peers):
    device = peers["device"]

    device.write("?")
    device.expect_exact("DEVICE_READY 1")
    dut.write("?")
    dut.expect_exact("HOST_READY 1 vid=303a pid=4008")

    for check in (_axes, _hat, _buttons):
        check(dut, device)
