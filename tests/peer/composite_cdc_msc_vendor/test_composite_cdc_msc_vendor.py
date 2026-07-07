def test_composite_cdc_msc_vendor_enumerates(dut, peers):
    device = peers["device"]

    device.write("b")
    device.expect_exact("DEVICE_BEGIN ok ESP_OK")
    device.write("?")
    device.expect_exact("DEVICE_READY")

    dut.expect_exact("HOST_CONNECTED vid=303a pid=4023")

    # Three non-HID classes all drawing endpoints from the core allocator:
    # every interface class present and claimed, no duplicate endpoint address.
    dut.write("e")
    dut.expect(r"HOST_ENUM pid=4023 ifcount=\d+ eps=\d+ dup=0 cdc=[1-9]\d* msc=[1-9]\d* vendor=[1-9]\d* claimok=1")


def test_composite_cdc_msc_vendor_serial_works(dut, peers):
    device = peers["device"]

    device.write("d")
    device.expect_exact("DEVICE_TX 1")
    dut.expect_exact("SERIAL_RX device to host")

    dut.write("h")
    dut.expect_exact("SERIAL_TX 1")
    device.expect_exact("DEVICE_RX host to serial")


def test_composite_cdc_msc_vendor_msc_works(dut, peers):
    dut.write("m")
    dut.expect_exact("MSC_CAPACITY ok=1 blocks=16 block_size=512")


def test_composite_cdc_msc_vendor_vendor_works(dut, peers):
    # Bulk Vendor round-trip in the composite, driven entirely by the onRx
    # callback (no polling on the device). This is the regression guard for the
    # tud_vendor_rx_cb signature/linkage fix: before the fix the library defined
    # a 1-arg tud_vendor_rx_cb that got a C++-mangled symbol and never overrode
    # TinyUSB's weak default, so onRx never fired. See src/EspUsbDevice.cpp and
    # docs/DESIGN_NOTES.ja.md "複合時の vendor RX callback".
    device = peers["device"]

    dut.write("v")
    dut.expect_exact("VENDOR_ECHO ok=1 data=echo:ping")

    # onRx must actually have fired (rxtotal counts bytes received via the callback).
    device.write("q")
    device.expect(r"DEVICE_VENDOR_STATE onrx=[1-9]\d* rxtotal=[1-9]\d* avail=\d+")
