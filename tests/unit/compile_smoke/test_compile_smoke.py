def test_compile_smoke(dut):
    dut.expect_exact("TEST_BEGIN compile_smoke")
    dut.expect_exact("TEST_END")
    assert dut.expect_exact(["OK", "NG"]) == b"OK"
