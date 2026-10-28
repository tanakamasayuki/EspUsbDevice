def test_vendor_direct_off(dut):
    dut.expect_exact("TEST_BEGIN vendor_direct_off")
    dut.expect_exact("TEST_END")
    assert dut.expect_exact(["OK", "NG"]) == b"OK"
