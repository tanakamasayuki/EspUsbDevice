def test_usb_serial(dut, peers):
    """CDC ACM serial across two boards: both directions and the line coding API.

    One test rather than three. The three it replaces shared a single
    enumeration wait: only the first one waited for HOST_CONNECTED, and the
    other two wrote to the host immediately, which works while they run after it
    and races the enumeration when either is run on its own. Waiting once here,
    at the top, is what makes the whole sequence independent - and a sequence is
    what this is, so there was never much to gain by splitting it.
    """
    device = peers["device"]

    dut.expect_exact("HOST_CONNECTED vid=303a pid=4016")
    device.write("?")
    device.expect_exact("DEVICE_READY")

    # Device -> Host.
    device.write("d")
    device.expect_exact("DEVICE_TX 1")
    dut.expect_exact("SERIAL_RX device to host")

    # Host -> Device.
    dut.write("h")
    dut.expect_exact("SERIAL_TX 1")
    device.expect_exact("DEVICE_RX host to serial")

    # SET_LINE_CODING reaches the device, and data still flows afterwards.
    dut.write("c")
    dut.expect_exact("SERIAL_CONFIG 1")
    device.write("l")
    device.expect_exact("DEVICE_LINE_CODING seen=1 baud=57600 stop=2 parity=2 data=7")
    dut.write("h")
    dut.expect_exact("SERIAL_TX 1")
    device.expect_exact("DEVICE_RX host to serial")

    # Mark parity and 1.5 stop bits, which are the encodings a naive
    # implementation gets wrong.
    dut.write("m")
    dut.expect_exact("SERIAL_CONFIG_MARK 1")
    device.write("l")
    device.expect_exact("DEVICE_LINE_CODING seen=1 baud=300 stop=1 parity=3 data=5")

    # Baud alone, leaving the other fields as the previous request set them.
    dut.write("b")
    dut.expect_exact("SERIAL_BAUD 1")
    device.write("l")
    device.expect_exact("DEVICE_LINE_CODING seen=1 baud=115200 stop=1 parity=3 data=5")
