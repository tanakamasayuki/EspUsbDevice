def test_loopback_hid_vendor(dut):
    dut.expect_exact("HOST_DEVICE")
    dut.expect_exact("VENDOR_INPUT_TX 1")
    dut.expect_exact("VENDOR hello vendor")
    dut.expect_exact("SEND_FEATURE 1")
    dut.expect_exact("DEVICE_FEATURE id=6 len=63 host feature")
    dut.expect_exact("SEND_OUTPUT 1")
    dut.expect_exact("DEVICE_OUTPUT id=0 len=63 host output")
    dut.expect_exact("TEST_END ok")
    assert dut.expect_exact(["OK", "NG"]) == b"OK"
