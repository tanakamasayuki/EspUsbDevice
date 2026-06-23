def test_hid_vendor(dut, peers):
    device = peers["device"]
    device.expect_exact("DEVICE_BEGIN 1")
    dut.expect_exact("HOST_CONNECTED")

    device.write("h")
    device.expect_exact("CMD h 1")
    dut.expect_exact("VENDOR hello vendor")

    dut.write("f")
    dut.expect_exact("SEND_FEATURE 1")
    device.expect_exact("DEVICE_FEATURE id=6 len=63 host feature")

    dut.write("o")
    dut.expect_exact("SEND_OUTPUT 1")
    device.expect_exact("DEVICE_OUTPUT")
