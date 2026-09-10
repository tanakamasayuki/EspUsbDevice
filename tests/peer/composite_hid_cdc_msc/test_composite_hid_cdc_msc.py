def test_composite_hid_cdc_msc(dut, peers):
    """HID + CDC + MSC in one device, each function exercised in turn.

    One test rather than four. Only the first of the four waited for
    enumeration; the rest wrote to the host straight away, which works while
    they follow it and races the enumeration when one is run on its own.
    """
    device = peers["device"]

    # begin() status over UART first (independent of USB enumeration).
    device.write("b")
    device.expect_exact("DEVICE_BEGIN ok ESP_OK")
    device.write("?")
    device.expect_exact("DEVICE_READY")

    dut.expect_exact("HOST_CONNECTED vid=303a pid=4022")

    # Three classes (the max that fits the S3 endpoint budget): all interface
    # classes present and claimed, no duplicate endpoint address. If this holds,
    # every 2-class subset holds too.
    dut.write("e")
    dut.expect(r"HOST_ENUM pid=4022 ifcount=\d+ eps=\d+ dup=0 hid=[1-9]\d* cdc=[1-9]\d* msc=[1-9]\d* claimok=1")

    # HID.
    device.write("k")
    device.expect_exact("DEVICE_KEY 1")
    dut.expect_exact("KEY a")

    # CDC, both directions.
    device.write("d")
    device.expect_exact("DEVICE_TX 1")
    dut.expect_exact("SERIAL_RX device to host")

    dut.write("h")
    dut.expect_exact("SERIAL_TX 1")
    device.expect_exact("DEVICE_RX host to serial")

    # MSC.
    dut.write("m")
    dut.expect_exact("MSC_CAPACITY ok=1 blocks=16 block_size=512")
