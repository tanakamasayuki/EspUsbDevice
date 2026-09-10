"""Keyboard and mouse from one HID interface, across two boards.

Two report IDs on a single interface, which is the case where a report ID that is
dropped or misapplied turns a keystroke into a mouse move. The first byte of each
HID_INPUT line is that report ID, so it is asserted rather than left implicit.

The preconditions are asked rather than awaited: the device answers '?' once the
host has configured it, the host answers '?' once it has enumerated the peer, and
'D' replays the report descriptor summary the host fetched at enumeration. The
boot banners this used to read are printed once and only ever worked for a test
that ran first.

The cases are named functions driven from a list. Each leaves the mouse at rest
with no button held, so the order is not load-bearing.
"""


def _report_descriptor(dut, device):
    """One HID interface, whose descriptor carries both collections."""
    dut.write("D")
    dut.expect_exact("HID_DESC iface=0")


def _keyboard(dut, device):
    """Report ID 1: a keystroke, decoded to text by the host."""
    device.write("k")
    device.expect_exact("CMD k 1")
    dut.expect_exact("HID_INPUT iface=0 subclass=0 protocol=0 len=9 data=01 00 00 0e")
    dut.expect_exact("KEY k")


def _mouse_move(dut, device):
    """Report ID 2 from the same interface: motion, not a keystroke."""
    device.write("r")
    device.expect_exact("CMD r 1")
    dut.expect_exact("HID_INPUT iface=0 subclass=0 protocol=0 len=5 data=02 00 28 00 00")
    dut.expect_exact("MOUSE x=40 y=0 wheel=0 buttons=0 previous=0 moved=1 changed=0")


def _mouse_click(dut, device):
    device.write("m")
    device.expect_exact("CMD m 1")
    dut.expect_exact("buttons=1 previous=0 moved=0 changed=1")
    dut.expect_exact("buttons=0 previous=1 moved=0 changed=1")


def test_hid_keyboard_mouse(dut, peers):
    device = peers["device"]

    device.write("?")
    device.expect_exact("DEVICE_READY 1")
    dut.write("?")
    dut.expect_exact("HOST_READY 1 vid=303a pid=4003")

    for check in (_report_descriptor, _keyboard, _mouse_move, _mouse_click):
        check(dut, device)
