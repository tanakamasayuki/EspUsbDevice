def test_video_descriptor(dut):
    dut.expect_exact("TEST_BEGIN video_descriptor")
    dut.expect_exact("TEST_END")
    assert dut.expect_exact(["OK", "NG"]) == b"OK"
