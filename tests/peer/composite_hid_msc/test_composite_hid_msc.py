import pytest

# CONFIRMED KNOWN LIMITATION (2026-07): HID + MSC does not enumerate.
# EspUsbDevice enables HID with reserve_endpoints=false and numbers its HID
# endpoints (EP1 OUT / EP2 IN) with a private counter, so they are never
# registered in the Arduino-ESP32 core endpoint bitmask. MSC then draws EP1
# from tinyusb_get_free_duplex_endpoint(), colliding with the HID OUT endpoint,
# and device.begin() fails with ESP_FAIL. See
# docs/DESIGN_NOTES.ja.md "複合時の endpoint 採番衝突". Remove the xfail markers
# once HID endpoints are allocated from the core allocator.


@pytest.mark.xfail(reason="HID+MSC endpoint collision: begin() returns ESP_FAIL (see DESIGN_NOTES.ja.md)", strict=True)
def test_composite_hid_msc_enumerates(dut, peers):
    device = peers["device"]

    # begin() over UART first (independent of USB enumeration).
    device.write("b")
    device.expect_exact("DEVICE_BEGIN ok ESP_OK")
    device.write("?")
    device.expect_exact("DEVICE_READY")

    dut.expect_exact("HOST_CONNECTED vid=303a pid=4021")

    # Machine check: both interface classes claimed, no duplicate endpoint
    # address. HID uses a private endpoint counter while MSC draws from the
    # core allocator, so a collision surfaces here as dup=1 / claimok=0.
    dut.write("e")
    dut.expect(r"HOST_ENUM pid=4021 ifcount=\d+ eps=\d+ dup=0 hid=[1-9]\d* msc=[1-9]\d* claimok=1")


@pytest.mark.xfail(reason="HID+MSC endpoint collision: HID never mounts (see DESIGN_NOTES.ja.md)", strict=True)
def test_composite_hid_msc_keyboard_works(dut, peers):
    device = peers["device"]

    device.write("k")
    device.expect_exact("DEVICE_KEY 1")
    dut.expect_exact("KEY a")


@pytest.mark.xfail(reason="HID+MSC endpoint collision: MSC never mounts (see DESIGN_NOTES.ja.md)", strict=True)
def test_composite_hid_msc_capacity(dut, peers):
    dut.write("m")
    dut.expect_exact("MSC_CAPACITY ok=1 blocks=16 block_size=512")
