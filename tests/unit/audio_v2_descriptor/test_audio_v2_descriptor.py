def test_audio_v2_descriptor(dut):
    dut.expect_exact("TEST_BEGIN audio_v2_descriptor")
    dut.expect_exact("TEST_END")
    assert dut.expect_exact(["OK", "NG"]) == b"OK"
