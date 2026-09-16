def test_hid_registration_order(dut):
    dut.expect_exact("TEST_BEGIN hid_registration_order")
    dut.expect_exact("TEST_END")
    assert dut.expect_exact(["OK", "NG"]) == b"OK"
