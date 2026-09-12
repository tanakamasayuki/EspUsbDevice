def test_p4_hs_packet_sizes(dut):
    dut.expect_exact("TEST_BEGIN p4_hs_packet_sizes")
    dut.expect_exact("TEST_END pass=29 fail=0")
    assert dut.expect_exact(["OK", "NG"]) == b"OK"
