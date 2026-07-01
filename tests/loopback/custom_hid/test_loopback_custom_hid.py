def test_loopback_custom_hid(dut):
    dut.expect_exact("HOST_DEVICE")
    dut.expect_exact("HID_REPORT iface=0 reported=38 len=38 first=05 last=c0")
    dut.expect_exact("CUSTOM_TX 1")
    dut.expect_exact("CUSTOM len=8 data=017f812233445566")
    dut.expect_exact("TEST_END ok")
    assert dut.expect_exact(["OK", "NG"]) == b"OK"
