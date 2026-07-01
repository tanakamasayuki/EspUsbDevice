def expect_consumer_press(dut, usage):
    dut.expect_exact(f"CONSUMER usage=0x{usage:04x} pressed=1 released=0")


def expect_consumer_release(dut, usage):
    dut.expect_exact(f"CONSUMER usage=0x{usage:04x} pressed=0 released=1")


def test_loopback_hid_consumer_control(dut):
    dut.expect_exact("HOST_DEVICE")
    expect_consumer_press(dut, 0x00E9)
    dut.expect_exact("SEND volume_up 1")
    expect_consumer_release(dut, 0x00E9)
    expect_consumer_press(dut, 0x00EA)
    dut.expect_exact("SEND volume_down 1")
    expect_consumer_release(dut, 0x00EA)
    expect_consumer_press(dut, 0x00CD)
    dut.expect_exact("SEND play_pause 1")
    expect_consumer_release(dut, 0x00CD)
    expect_consumer_press(dut, 0x00B5)
    dut.expect_exact("SEND next_track 1")
    expect_consumer_release(dut, 0x00B5)
    expect_consumer_press(dut, 0x00B6)
    dut.expect_exact("SEND previous_track 1")
    expect_consumer_release(dut, 0x00B6)
    expect_consumer_press(dut, 0x00E2)
    dut.expect_exact("SEND mute 1")
    expect_consumer_release(dut, 0x00E2)
    dut.expect_exact("TEST_END ok")
    assert dut.expect_exact(["OK", "NG"]) == b"OK"
