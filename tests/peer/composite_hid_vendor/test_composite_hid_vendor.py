def test_composite_hid_vendor_enumerates(dut, peers):
    device = peers["device"]

    device.write("b")
    device.expect_exact("DEVICE_BEGIN ok ESP_OK")
    device.write("?")
    device.expect_exact("DEVICE_READY")

    dut.expect_exact("HOST_CONNECTED vid=303a pid=4024")

    # HID interface + bulk Vendor interface both present and claimed, no duplicate
    # endpoint address, and distinct interface numbers. Before the descriptor
    # duplication fix the Vendor interface was emitted twice (once inside the HID
    # blob, once via the core's vendor loader), which shows up as dup=1 / ifnumdup=1
    # / claimok=0 or a failed enumeration.
    dut.write("e")
    dut.expect(
        r"HOST_ENUM pid=4024 ifcount=\d+ eps=\d+ dup=0 hid=[1-9]\d* vendor=1 ifnumdup=0 claimok=1"
    )


def test_composite_hid_vendor_keyboard_works(dut, peers):
    device = peers["device"]

    device.write("k")
    device.expect_exact("DEVICE_KEY 1")
    dut.expect_exact("KEY a")


def test_composite_hid_vendor_vendor_works(dut, peers):
    # Bulk Vendor round-trip driven entirely by the onRx callback (no polling).
    device = peers["device"]

    dut.write("v")
    dut.expect_exact("VENDOR_ECHO ok=1 data=echo:ping")

    # onRx must actually have fired.
    device.write("q")
    device.expect(r"DEVICE_VENDOR_STATE onrx=[1-9]\d* rxtotal=[1-9]\d* avail=\d+")
