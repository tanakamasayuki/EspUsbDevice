def test_loopback_hid_keyboard_layout(dut):
    dut.expect_exact("TEST_BEGIN loopback_hid_keyboard_layout")
    dut.expect_exact("HOST_READY fs")
    dut.expect_exact("DEVICE_READY fs")
    dut.expect_exact("HOST_DEVICE")

    dut.expect_exact("LAYOUT EN_US")
    for key in ["@", "^"]:
        dut.expect_exact(f"KEY {key}")

    dut.expect_exact("LAYOUT JA_JP")
    for key in ["@", "^", ":", "_"]:
        dut.expect_exact(f"KEY {key}")

    dut.expect_exact("TEST_END ok")
    assert dut.expect_exact(["OK", "NG"]) == b"OK"
