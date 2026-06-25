def set_layout(dut, device, command, name):
    dut.write(command)
    dut.expect_exact(f"HOST_LAYOUT {name}")
    device.write(command)
    device.expect_exact(f"DEVICE_LAYOUT {name}")


def expect_key(dut, device, key):
    device.write(key)
    device.expect_exact(f"SEND {key} 1")
    dut.expect_exact(f"KEY {key}")


def test_hid_keyboard_layout_en_us(dut, peers):
    device = peers["device"]
    device.expect_exact("DEVICE_BEGIN 1")
    dut.expect_exact("HOST_CONNECTED")

    set_layout(dut, device, "E", "EN_US")
    for key in ["@", "^"]:
        expect_key(dut, device, key)


def test_hid_keyboard_layout_ja_jp(dut, peers):
    device = peers["device"]

    set_layout(dut, device, "J", "JA_JP")
    for key in ["@", "^", ":", "_"]:
        expect_key(dut, device, key)
