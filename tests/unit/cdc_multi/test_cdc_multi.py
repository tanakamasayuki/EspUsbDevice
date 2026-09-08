def test_cdc_multi(dut):
    dut.expect_exact("TEST_BEGIN cdc_multi")
    dut.expect_exact("TEST_END")
    assert dut.expect_exact(["OK", "NG"]) == b"OK"
