def test_hid_keyboard_mouse_composite(dut, peers):
    device = peers["device"]
    device.expect_exact("DEVICE_BEGIN 1")
    dut.expect_exact("HOST_CONNECTED")
    dut.expect_exact("HID_DESC iface=0")

    device.write("k")
    device.expect_exact("CMD k 1")
    dut.expect_exact("HID_INPUT iface=0 subclass=0 protocol=0 len=9 data=01 00 00 0e")
    dut.expect_exact("KEY k")

    device.write("r")
    device.expect_exact("CMD r 1")
    dut.expect_exact("HID_INPUT iface=0 subclass=0 protocol=0 len=5 data=02 00 28 00 00")
    dut.expect_exact("MOUSE x=40 y=0 wheel=0 buttons=0 previous=0 moved=1 changed=0")

    device.write("m")
    device.expect_exact("CMD m 1")
    dut.expect_exact("buttons=1 previous=0 moved=0 changed=1")
    dut.expect_exact("buttons=0 previous=1 moved=0 changed=1")
