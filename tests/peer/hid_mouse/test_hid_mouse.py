import time


def test_hid_mouse_move(dut, peers):
    device = peers["device"]
    device.expect_exact("DEVICE_BEGIN 1")
    dut.expect_exact("HOST_CONNECTED")

    device.write("r")
    device.expect_exact("CMD r 1")
    dut.expect_exact("MOUSE x=40 y=0 wheel=0 buttons=0 previous=0 moved=1 changed=0")

    device.write("l")
    device.expect_exact("CMD l 1")
    dut.expect_exact("MOUSE x=-40 y=0 wheel=0 buttons=0 previous=0 moved=1 changed=0")

    device.write("d")
    device.expect_exact("CMD d 1")
    dut.expect_exact("MOUSE x=0 y=40 wheel=0 buttons=0 previous=0 moved=1 changed=0")

    device.write("u")
    device.expect_exact("CMD u 1")
    dut.expect_exact("MOUSE x=0 y=-40 wheel=0 buttons=0 previous=0 moved=1 changed=0")

    device.write("w")
    device.expect_exact("CMD w 1")
    dut.expect_exact("MOUSE x=0 y=0 wheel=1 buttons=0 previous=0 moved=1 changed=0")


def test_hid_mouse_buttons(dut, peers):
    device = peers["device"]

    device.write("M")
    device.expect_exact("CMD M 1")
    dut.expect_exact("RAW len=4 04 00 00 00")
    dut.expect_exact("RAW len=4 00 00 00 00")
    time.sleep(0.1)

    device.write("m")
    device.expect_exact("CMD m 1")
    dut.expect_exact("buttons=1 previous=0 moved=0 changed=1")
    dut.expect_exact("buttons=0 previous=1 moved=0 changed=1")
    time.sleep(0.1)

    device.write("R")
    device.expect_exact("CMD R 1")
    dut.expect_exact("buttons=2 previous=0 moved=0 changed=1")
    dut.expect_exact("buttons=0 previous=2 moved=0 changed=1")
    time.sleep(0.1)

    device.write("b")
    device.expect_exact("CMD b 1")
    dut.expect_exact("RAW len=4 08 00 00 00")
    dut.expect_exact("RAW len=4 00 00 00 00")
    time.sleep(0.1)

    device.write("f")
    device.expect_exact("CMD f 1")
    dut.expect_exact("RAW len=4 10 00 00 00")
    dut.expect_exact("RAW len=4 00 00 00 00")
