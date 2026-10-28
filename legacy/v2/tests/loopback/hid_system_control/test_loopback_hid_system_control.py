def expect_system_press(dut, usage):
    dut.expect_exact(f"SYSTEM usage=0x{usage:02x} pressed=1 released=0")


def expect_system_release(dut, usage):
    dut.expect_exact(f"SYSTEM usage=0x{usage:02x} pressed=0 released=1")


def test_loopback_hid_system_control(dut):
    dut.expect_exact("HOST_DEVICE")
    expect_system_press(dut, 0x01)
    dut.expect_exact("SEND power_off 1")
    expect_system_release(dut, 0x01)
    expect_system_press(dut, 0x02)
    dut.expect_exact("SEND standby 1")
    expect_system_release(dut, 0x02)
    expect_system_press(dut, 0x03)
    dut.expect_exact("SEND wake_host 1")
    expect_system_release(dut, 0x03)
    dut.expect_exact("TEST_END ok")
    assert dut.expect_exact(["OK", "NG"]) == b"OK"
