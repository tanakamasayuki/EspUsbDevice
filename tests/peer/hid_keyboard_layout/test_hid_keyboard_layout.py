import time

import pexpect


def set_layout(dut, device, command, name):
    dut.write(command)
    dut.expect_exact(f"HOST_LAYOUT {name}")
    device.write(command)
    device.expect_exact(f"DEVICE_LAYOUT {name}")
    time.sleep(0.5)


def expect_key(dut, device, key):
    last_error = None
    for _ in range(3):
        device.write(key)
        device.expect_exact(f"SEND {key} 1")
        try:
            dut.expect_exact(f"KEY {key}", timeout=5)
            return
        except pexpect.TIMEOUT as err:
            last_error = err
    raise last_error


def test_hid_keyboard_layout_en_us(dut, peers):
    device = peers["device"]
    device.expect_exact("DEVICE_BEGIN 1")
    dut.expect_exact("HOST_CONNECTED vid=303a pid=4009")
    device.write("?")
    device.expect_exact("DEVICE_READY")
    time.sleep(1.0)

    set_layout(dut, device, "E", "EN_US")
    for key in ["@", "^"]:
        expect_key(dut, device, key)


def test_hid_keyboard_layout_ja_jp(dut, peers):
    device = peers["device"]
    device.write("?")
    device.expect_exact("DEVICE_READY")

    set_layout(dut, device, "J", "JA_JP")
    for key in ["@", "^", ":", "_"]:
        expect_key(dut, device, key)
