"""HID keyboard across two boards: typing, host LED output, and SET_PROTOCOL.

One test rather than four. The four it replaced shared a single enumeration
wait: only the first waited for DEVICE_BEGIN / HOST_CONNECTED / HID_DESC, and
the rest wrote to the host immediately, which works while they run after it and
races the enumeration when any of them is run on its own. Those three lines are
printed once at boot, so even the first one only worked by being first.

Both sketches answer rather than announce now. The device types every printable
byte it receives, so its commands are control bytes: 0x04 asks whether the host
has configured it. The host answers '?' with the peer's address and 'D' with the
report descriptor summary it fetched at enumeration.

The cases are named functions driven from a list, so a failure names the
function it happened in. Each one establishes what it needs and leaves the LED
state cleared, so the list order is not load-bearing.
"""


def _report_descriptor(dut, device):
    """The host fetched a report descriptor for the keyboard interface.

    Everything below decodes through it, and _set_protocol addresses the
    interface number it carries, so it is worth stating on its own.
    """
    dut.write("D")
    dut.expect_exact("HID_DESC iface=0")


def _typing(dut, device):
    """Typing reaches the host as boot-protocol reports and decodes to text."""
    text = "hello, keyboard"
    device.write(text)
    dut.expect_exact("HID_INPUT iface=0 subclass=1 protocol=1 len=8 data=00 00 0b")
    dut.expect_exact(text)


def _led_callback(dut, device):
    """Host -> device LED output reports arrive through the callback."""
    dut.write("n")
    dut.expect_exact("LED_TX 1")
    device.expect_exact("LED numlock=1 capslock=0 scrolllock=0")

    dut.write("c")
    dut.expect_exact("LED_TX 1")
    device.expect_exact("LED numlock=0 capslock=1 scrolllock=0")

    dut.write("s")
    dut.expect_exact("LED_TX 1")
    device.expect_exact("LED numlock=0 capslock=0 scrolllock=1")

    dut.write("0")
    dut.expect_exact("LED_TX 1")
    device.expect_exact("LED numlock=0 capslock=0 scrolllock=0")


def _led_state_getter(dut, device):
    """ledState() reports the same host LED state as the callback, and without it.

    The callback is a single slot: an integration layer that takes it leaves the
    sketch with no way to read Lock state, which is what the getter is for. So
    the state must keep tracking the host with no callback installed at all.
    Control bytes are commands to peer_device.ino: 0x01 print ledState(),
    0x02 drop the callback, 0x03 reinstall it.
    """
    dut.write("c")
    dut.expect_exact("LED_TX 1")
    device.expect_exact("LED numlock=0 capslock=1 scrolllock=0")
    device.write("\x01")
    device.expect_exact("LED_STATE numlock=0 capslock=1 scrolllock=0 raw=0x02")

    device.write("\x02")
    device.expect_exact("LED_CALLBACK_CLEARED")

    dut.write("n")
    dut.expect_exact("LED_TX 1")
    device.write("\x01")
    device.expect_exact("LED_STATE numlock=1 capslock=0 scrolllock=0 raw=0x01")

    dut.write("s")
    dut.expect_exact("LED_TX 1")
    device.write("\x01")
    device.expect_exact("LED_STATE numlock=0 capslock=0 scrolllock=1 raw=0x04")

    dut.write("0")
    dut.expect_exact("LED_TX 1")
    device.write("\x01")
    device.expect_exact("LED_STATE numlock=0 capslock=0 scrolllock=0 raw=0x00")

    # Put the callback back, so this case leaves the sketch as it found it.
    device.write("\x03")
    device.expect_exact("LED_CALLBACK_INSTALLED")


def _set_protocol(dut, device):
    dut.write("p")
    dut.expect("PROTOCOL_TX 1 iface=0 address=[1-9][0-9]*")
    device.expect_exact("PROTOCOL instance=0 protocol=1")


def test_hid_keyboard(dut, peers):
    device = peers["device"]

    device.write("\x04")
    device.expect_exact("DEVICE_READY 1")
    dut.write("?")
    dut.expect(r"HOST_READY 1 address=[1-9]\d*")

    for check in (_report_descriptor, _typing, _led_callback, _led_state_getter, _set_protocol):
        check(dut, device)
