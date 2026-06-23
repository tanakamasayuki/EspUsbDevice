def expect_system_click(dut, device, command, usage):
    device.write(command)
    device.expect_exact(f"CMD {command} 1")
    dut.expect_exact(f"SYSTEM usage=0x{usage:02x} pressed=1 released=0")
    dut.expect_exact(f"SYSTEM usage=0x{usage:02x} pressed=0 released=1")


def test_hid_system_control(dut, peers):
    device = peers["device"]
    device.expect_exact("DEVICE_BEGIN 1")
    dut.expect_exact("HOST_CONNECTED")

    expect_system_click(dut, device, "p", 0x01)
    expect_system_click(dut, device, "s", 0x02)
