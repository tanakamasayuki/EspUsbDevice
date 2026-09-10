def test_hid_keyboard(dut, peers):
    """HID keyboard across two boards: typing, host LED output, and SET_PROTOCOL.

    One test rather than four. The four it replaces shared a single enumeration
    wait: only the first waited for DEVICE_BEGIN / HOST_CONNECTED / HID_DESC,
    and the rest wrote to the host immediately, which works while they run after
    it and races the enumeration when any of them is run on its own.

    The LED section covers `keyboard.ledState()` as well as the callback,
    because the callback is a single slot: an integration layer that takes it
    leaves the sketch with no way to read Lock state, which is what the getter
    is for. So the state must keep tracking the host with no callback at all.
    Control bytes are commands to peer_device.ino: 0x01 print ledState(),
    0x02 drop the callback, 0x03 reinstall it.
    """
    device = peers["device"]

    device.expect_exact("DEVICE_BEGIN 1")
    dut.expect_exact("HOST_CONNECTED")
    dut.expect_exact("HID_DESC iface=0")

    # Typing reaches the host as boot-protocol reports and decodes to text.
    text = "hello, keyboard"
    device.write(text)
    dut.expect_exact("HID_INPUT iface=0 subclass=1 protocol=1 len=8 data=00 00 0b")
    dut.expect_exact(text)

    # Host -> device LED output reports, through the callback.
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

    # ledState() must agree with the callback while the callback is installed.
    dut.write("c")
    dut.expect_exact("LED_TX 1")
    device.expect_exact("LED numlock=0 capslock=1 scrolllock=0")
    device.write("\x01")
    device.expect_exact("LED_STATE numlock=0 capslock=1 scrolllock=0 raw=0x02")

    # With no callback at all, the state must still follow the host.
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

    device.write("\x03")
    device.expect_exact("LED_CALLBACK_INSTALLED")

    # SET_PROTOCOL reaches the device.
    dut.write("p")
    dut.expect("PROTOCOL_TX 1 iface=0 address=[1-9][0-9]*")
    device.expect_exact("PROTOCOL instance=0 protocol=1")
