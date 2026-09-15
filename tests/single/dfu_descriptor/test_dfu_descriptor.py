def test_dfu_descriptor(dut):
    dut.expect_exact("TEST_BEGIN dfu_descriptor")
    dut.expect_exact("TEST_END")
    assert dut.expect_exact(["OK", "NG"]) == b"OK"
