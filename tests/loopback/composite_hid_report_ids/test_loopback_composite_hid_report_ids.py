def test_loopback_composite_hid_report_ids(dut):
    dut.expect_exact("HOST_DEVICE")
    dut.expect_exact("MERGED well_formed=1 collections=2 report_ids=01,06 stray=0")
    dut.expect_exact("KEYBOARD_REPORT id=1 len=9")
    dut.expect_exact("VENDOR_REPORT id=6 len=16")
    dut.expect_exact("SEND_FEATURE 1")
    dut.expect_exact("DEVICE_FEATURE id=6 len=15")
    dut.expect_exact("SEND_LED 1")
    dut.expect_exact("DEVICE_LED numlock=1 capslock=0")
    dut.expect_exact("TEST_END ok")
    assert dut.expect_exact(["OK", "NG"]) == b"OK"
