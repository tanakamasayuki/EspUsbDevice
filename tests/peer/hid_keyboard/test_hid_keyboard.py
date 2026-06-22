def test_hid_keyboard_text(dut, peers):
    device = peers["device"]
    device.expect_exact("DEVICE_BEGIN 1")
    dut.expect_exact("HOST_CONNECTED")

    text = "hello, keyboard"
    device.write(text)
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
