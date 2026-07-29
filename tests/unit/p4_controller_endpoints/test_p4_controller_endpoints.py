def test_p4_controller_endpoints(dut):
    dut.expect_exact("TEST_BEGIN p4_controller_endpoints")
    dut.expect_exact("TEST_END pass=6 fail=0")
    assert dut.expect_exact(["OK", "NG"]) == b"OK"
