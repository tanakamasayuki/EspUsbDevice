def expect_gamepad_report(dut, device, command, report):
    device.write(command)
    device.expect_exact(f"CMD {command} 1")
    dut.expect(f"GAMEPAD report={report} fields=[1-9][0-9]*")


def test_hid_gamepad_axes(dut, peers):
    device = peers["device"]
    device.expect_exact("DEVICE_BEGIN 1")
    dut.expect_exact("HOST_CONNECTED")

    expect_gamepad_report(dut, device, "a", "0a f6 14 ec 1e e2 03 05 00 00 00")
    expect_gamepad_report(dut, device, "0", "00 00 00 00 00 00 00 00 00 00 00")


def test_hid_gamepad_hat(dut, peers):
    device = peers["device"]

    for command, hat in [("1", 1), ("2", 2), ("3", 3), ("4", 4), ("5", 5), ("6", 6), ("7", 7), ("8", 8)]:
        expect_gamepad_report(dut, device, command, f"00 00 00 00 00 00 {hat:02x} 00 00 00 00")
        expect_gamepad_report(dut, device, "0", "00 00 00 00 00 00 00 00 00 00 00")


def test_hid_gamepad_buttons(dut, peers):
    device = peers["device"]

    expect_gamepad_report(dut, device, "b", "00 00 00 00 00 00 00 ff 7f 00 00")
    expect_gamepad_report(dut, device, "0", "00 00 00 00 00 00 00 00 00 00 00")
