"""Mouse movement and buttons, across two boards.

One test rather than two. The first read `DEVICE_BEGIN`, `HOST_CONNECTED` and
`HID_DESC` - all printed once at boot - and the second inherited the enumeration
it had waited for.

The host sketch is event-driven, so it had nothing to ask. It has two commands
now: '?' answers with the peer's identity once enumerated, and 'D' replays the
report descriptor summary, which arrives once when the host fetches it.

The cases are named functions driven from a list. A move leaves no state the
next case reads, and each click is released before it returns, so the order is
not load-bearing.
"""

import time


def _report_descriptor(dut, device):
    """The host fetched a report descriptor for the mouse interface.

    Everything below is decoded through it, so it is worth stating separately:
    if this is wrong the MOUSE lines would be wrong in a way that looks like a
    device-side bug.
    """
    dut.write("D")
    dut.expect_exact("HID_DESC iface=0")


def _move(dut, device):
    """Each direction, and the wheel. `moved=1 changed=0` says the host saw
    motion without a button transition."""
    device.write("r")
    device.expect_exact("CMD r 1")
    dut.expect_exact("HID_INPUT iface=0 subclass=1 protocol=2 len=4 data=00 28 00 00")
    dut.expect_exact("MOUSE x=40 y=0 wheel=0 buttons=0 previous=0 moved=1 changed=0")

    device.write("l")
    device.expect_exact("CMD l 1")
    dut.expect_exact("MOUSE x=-40 y=0 wheel=0 buttons=0 previous=0 moved=1 changed=0")

    device.write("d")
    device.expect_exact("CMD d 1")
    dut.expect_exact("MOUSE x=0 y=40 wheel=0 buttons=0 previous=0 moved=1 changed=0")

    device.write("u")
    device.expect_exact("CMD u 1")
    dut.expect_exact("MOUSE x=0 y=-40 wheel=0 buttons=0 previous=0 moved=1 changed=0")

    device.write("w")
    device.expect_exact("CMD w 1")
    dut.expect_exact("MOUSE x=0 y=0 wheel=1 buttons=0 previous=0 moved=1 changed=0")


def _buttons(dut, device):
    """Every button, press and release, checked through `previous=` so a stuck
    bit from the preceding click would fail here rather than pass quietly.

    The pause between clicks keeps the two boards from printing on top of each
    other at 115200 baud; the device already spaces press and release itself.
    """
    for command, mask in [("M", 4), ("m", 1), ("R", 2), ("b", 8), ("f", 16)]:
        device.write(command)
        device.expect_exact(f"CMD {command} 1")
        dut.expect_exact(f"buttons={mask} previous=0 moved=0 changed=1")
        dut.expect_exact(f"buttons=0 previous={mask} moved=0 changed=1")
        time.sleep(0.1)


def test_hid_mouse(dut, peers):
    device = peers["device"]

    device.write("?")
    device.expect_exact("DEVICE_READY 1")
    dut.write("?")
    dut.expect_exact("HOST_READY 1 vid=303a pid=4002")

    for check in (_report_descriptor, _move, _buttons):
        check(dut, device)
