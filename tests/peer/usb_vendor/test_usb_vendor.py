def test_usb_vendor_enumeration(dut, peers):
    device = peers["device"]

    dut.expect_exact("HOST_CONNECTED")
    device.write("?")
    device.expect_exact("DEVICE_READY")

    dut.write("i")
    dut.expect_exact("INTERFACE number=0 class=0xff subclass=0x00 protocol=0x00 endpoints=2")
    dut.expect_exact("ENDPOINT iface=0 ep=0x01 attrs=0x02 mps=64 interval=0")
    dut.expect_exact("ENDPOINT iface=0 ep=0x81 attrs=0x02 mps=64 interval=0")
    dut.expect_exact("VENDOR_ENUM interface=1 bulk_out=1 bulk_in=1")

    device.write("s")
    device.expect_exact("DEVICE_STATUS rx=0 control=0")
