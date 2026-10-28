def test_loopback_usb_serial(dut):
    dut.expect_exact("HOST_DEVICE")
    dut.expect_exact("DEVICE_TX 1")
    dut.expect_exact("SERIAL_RX device to host")
    dut.expect_exact("SERIAL_TX 1")
    dut.expect_exact("DEVICE_RX host to serial")
    dut.expect_exact("SERIAL_CONFIG 1")
    dut.expect_exact("DEVICE_LINE_CODING seen=1 baud=57600 stop=2 parity=2 data=7")
    dut.expect_exact("SERIAL_CONFIG_MARK 1")
    dut.expect_exact("DEVICE_LINE_CODING seen=1 baud=300 stop=1 parity=3 data=5")
    dut.expect_exact("SERIAL_BAUD 1")
    dut.expect_exact("DEVICE_LINE_CODING seen=1 baud=115200 stop=1 parity=3 data=5")
    dut.expect_exact("TEST_END ok")
    assert dut.expect_exact(["OK", "NG"]) == b"OK"
