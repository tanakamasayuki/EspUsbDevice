def test_descriptor(dut):
    dut.expect_exact("TEST_BEGIN descriptor")
    dut.expect_exact("TEST_END")
    assert dut.expect_exact(["OK", "NG"]) == b"OK"
