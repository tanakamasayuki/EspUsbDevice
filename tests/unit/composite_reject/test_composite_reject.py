def test_composite_reject(dut):
    dut.expect_exact("TEST_BEGIN composite_reject")
    dut.expect_exact("TEST_END")
    assert dut.expect_exact(["OK", "NG"]) == b"OK"
