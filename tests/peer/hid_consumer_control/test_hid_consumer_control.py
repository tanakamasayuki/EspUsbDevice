def expect_consumer_click(dut, device, command, usage):
    device.write(command)
    device.expect_exact(f"CMD {command} 1")
    dut.expect_exact(f"CONSUMER usage=0x{usage:04x} pressed=1 released=0")
    dut.expect_exact(f"CONSUMER usage=0x{usage:04x} pressed=0 released=1")


def test_hid_consumer_control(dut, peers):
    device = peers["device"]
    device.expect_exact("DEVICE_BEGIN 1")
    dut.expect_exact("HOST_CONNECTED")

    expect_consumer_click(dut, device, "u", 0x00E9)
    expect_consumer_click(dut, device, "d", 0x00EA)
    expect_consumer_click(dut, device, "p", 0x00CD)
    expect_consumer_click(dut, device, "n", 0x00B5)
    expect_consumer_click(dut, device, "s", 0x00B6)
    expect_consumer_click(dut, device, "m", 0x00E2)
