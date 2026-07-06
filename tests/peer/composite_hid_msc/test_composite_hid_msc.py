def test_composite_hid_msc_enumerates(dut, peers):
    device = peers["device"]

    # Query begin() over UART first (independent of USB enumeration).
    device.write("b")
    device.expect_exact("DEVICE_BEGIN ok ESP_OK")
    device.write("?")
    device.expect_exact("DEVICE_READY")

    dut.expect_exact("HOST_CONNECTED vid=303a pid=4021")

    # Machine check: both interface classes claimed, no duplicate endpoint
    # address. HID now takes EP1 (reserved in the core bitmask) and MSC draws
    # EP2 from the allocator, so there is no collision. Regression guard for
    # docs/DESIGN_NOTES.ja.md "複合時の endpoint 採番衝突".
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
