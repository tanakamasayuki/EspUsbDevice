def test_usb_vendor_enumeration_and_transfer(dut, peers):
    device = peers["device"]

    dut.expect_exact("HOST_CONNECTED")
    device.write("?")
    device.expect_exact("DEVICE_READY")

    dut.write("i")
    dut.expect_exact("INTERFACE number=0 class=0xff subclass=0x00 protocol=0x00 endpoints=2")
    dut.expect_exact("ENDPOINT iface=0 ep=0x01 attrs=0x02 mps=64 interval=0")
    dut.expect_exact("ENDPOINT iface=0 ep=0x81 attrs=0x02 mps=64 interval=0")
    dut.expect_exact("VENDOR_ENUM interface=1 bulk_out=1 bulk_in=1")

    dut.write("o")
    dut.expect_exact("VENDOR_OPEN 1")

    dut.write("w")
    dut.expect_exact("VENDOR_WRITE 1")
    dut.write("p")
    dut.expect_exact("VENDOR_DATA seen=1 data=echo:ping")

    dut.write("r")
    dut.expect_exact("VENDOR_READ len=9 data=echo:ping")

    dut.write("c")
    dut.expect_exact("VENDOR_CONTROL_IN ok=1 len=18 data=EspUsbDeviceVendor")

    dut.write("C")
    dut.expect_exact("VENDOR_CONTROL_OUT 1")

    dut.write("u")
    dut.expect("WEBUSB_URL ok=1 len=[1-9][0-9]* found=1")

    device.write("s")
    device.expect("DEVICE_STATUS rx=4 control=[1-9][0-9]*")
