def test_vendor_direct(dut):
    dut.expect_exact("TEST_BEGIN vendor_direct")
    dut.expect_exact("TEST_END")
    assert dut.expect_exact(["OK", "NG"]) == b"OK"
