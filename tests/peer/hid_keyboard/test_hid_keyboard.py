def test_hid_keyboard_text(dut, peers):
    device = peers["device"]
    device.expect_exact("DEVICE_BEGIN 1")
    dut.expect_exact("HOST_CONNECTED")
    dut.expect_exact("HID_DESC iface=0")

    text = "hello, keyboard"
    device.write(text)
    dut.expect_exact("HID_INPUT iface=0 subclass=1 protocol=1 len=8 data=00 00 0b")
    dut.expect_exact(text)


def test_hid_keyboard_led(dut, peers):
    device = peers["device"]

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


def test_hid_keyboard_set_protocol(dut, peers):
    device = peers["device"]

    dut.write("p")
    dut.expect("PROTOCOL_TX 1 iface=0 address=[1-9][0-9]*")
    device.expect_exact("PROTOCOL instance=0 protocol=1")
