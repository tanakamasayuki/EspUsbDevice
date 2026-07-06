def test_composite_hid_cdc_enumerates(dut, peers):
    device = peers["device"]

    # Wait for our device (not the transient USB-Serial/JTAG) to enumerate.
    # The host filters onDeviceConnected to pid=4020, so this is reliable
    # regardless of how long EspUsbDevice takes to take over the OTG port.
    dut.expect_exact("HOST_CONNECTED vid=303a pid=4020")

    # Device brings up both classes without hitting the Audio-exclusive /
    # MAX_CLASSES guards. Queried on demand ('b') rather than relying on the
    # boot-time line, which can scroll past before capture starts.
    device.write("b")
    device.expect_exact("DEVICE_BEGIN ok ESP_OK")
    device.write("?")
    device.expect_exact("DEVICE_READY")

    # Host-side machine check: talking to our device, both interface classes
    # claimed, no duplicate endpoint address (a composite EP-allocation
    # collision would surface here as dup=1 or claimok=0).
    dut.write("e")
    dut.expect(r"HOST_ENUM pid=4020 ifcount=\d+ eps=\d+ dup=0 hid=[1-9]\d* cdc=[1-9]\d* claimok=1")


def test_composite_hid_cdc_keyboard_works(dut, peers):
    device = peers["device"]

    device.write("k")
    device.expect_exact("DEVICE_KEY 1")
    dut.expect_exact("KEY a")


def test_composite_hid_cdc_serial_device_to_host(dut, peers):
    device = peers["device"]

    device.write("d")
    device.expect_exact("DEVICE_TX 1")
    dut.expect_exact("SERIAL_RX device to host")


def test_composite_hid_cdc_serial_host_to_device(dut, peers):
    device = peers["device"]

    dut.write("h")
    dut.expect_exact("SERIAL_TX 1")
    device.expect_exact("DEVICE_RX host to serial")
