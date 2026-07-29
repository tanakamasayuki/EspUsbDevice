def test_composite_constraints(dut):
    dut.expect_exact("TEST_BEGIN composite_constraints")
    dut.expect_exact("TEST_END")
    assert dut.expect_exact(["OK", "NG"]) == b"OK"
