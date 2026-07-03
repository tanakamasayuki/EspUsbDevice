def test_loopback_usb_audio(dut):
    dut.expect_exact("TEST_BEGIN loopback_usb_audio")
    dut.expect_exact("HOST_READY hs")
    dut.expect_exact("DEVICE_READY hs")
    dut.expect_exact("HOST_DEVICE")
    dut.expect("AUDIO_OUT_READY addr=[0-9]+")
    # P4 runs the high-speed / UAC2 descriptor path, so the exact format fields
    # differ from the UAC1 peer test. Assert that an OUT stream is discovered and
    # that PCM actually reaches the device rather than the exact format numbers.
    dut.expect("AUDIO_STREAM iface=[0-9]+ alt=[1-9][0-9]* ep=0x[0-9a-f]+ dir=OUT")
    dut.expect_exact("AUDIO_START 1")
    dut.expect_exact("AUDIO_INTERFACE SPK 1")
    dut.expect_exact("AUDIO_RESET")
    dut.expect("AUDIO_TX [1-9][0-9]*")
    dut.expect("DEVICE_RX_AUDIO [1-9][0-9]*")
    dut.expect_exact("TEST_END ok")
    assert dut.expect_exact(["OK", "NG"]) == b"OK"
