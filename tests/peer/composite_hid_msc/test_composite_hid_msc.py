def test_composite_hid_msc_enumerates(dut, peers):
    device = peers["device"]

    dut.expect_exact("HOST_CONNECTED vid=303a pid=4021")

    device.write("b")
    device.expect_exact("DEVICE_BEGIN ok ESP_OK")
    device.write("?")
    device.expect_exact("DEVICE_READY")

    # Machine check: both interface classes claimed, no duplicate endpoint
    # address. HID uses a private endpoint counter while MSC draws from the
    # core allocator, so a collision would surface here as dup=1 / claimok=0.
    dut.write("e")
    dut.expect(r"HOST_ENUM pid=4021 ifcount=\d+ eps=\d+ dup=0 hid=[1-9]\d* msc=[1-9]\d* claimok=1")


def test_composite_hid_msc_keyboard_works(dut, peers):
    device = peers["device"]

    device.write("k")
    device.expect_exact("DEVICE_KEY 1")
    dut.expect_exact("KEY a")


def test_composite_hid_msc_capacity(dut, peers):
    dut.write("m")
    dut.expect_exact("MSC_CAPACITY ok=1 blocks=16 block_size=512")
