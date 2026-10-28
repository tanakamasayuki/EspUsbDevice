def test_bulk_in_fifo(dut):
    dut.expect_exact("TEST_BEGIN bulk_in_fifo")
    dut.expect_exact("TEST_END")
    assert dut.expect_exact(["OK", "NG"]) == b"OK"
