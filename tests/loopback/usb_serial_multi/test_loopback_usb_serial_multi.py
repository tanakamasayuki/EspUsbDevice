def test_loopback_usb_serial_multi(dut):
    # Matching starts at HOST_DEVICE, not at the sketch's TEST_BEGIN: everything
    # printed before the host enumerates the device races the serial capture
    # attaching. The sketch repeats the port identities after enumeration for
    # the same reason.
    dut.expect_exact("HOST_DEVICE vid=0x303a pid=0x4019")

    # Three ports came up on the high-speed controller: begin() accepted the
    # descriptor against the endpoint budget, and the port indices follow
    # descriptor order.
    dut.expect_exact("DEVICE_PORTS max=3 p0=0 p1=1 p2=2")

    # What the host parsed: 6 interfaces, 3 notification IN + 6 bulk, no
    # duplicate endpoint address. This is the six-IN-endpoint allocation on the
    # HS controller actually succeeding, not just a well-formed descriptor.
    dut.expect_exact("HOST_ENUM ifcount=6 ctrl=3 data=3 eps=9 bulk=6 intr=3 dup=0")
    dut.expect_exact("HOST_CLASS class=ef sub=02 proto=01")

    # Port 0 carries traffic both ways.
    dut.expect_exact("DEVICE_TX0 1")
    dut.expect_exact("SERIAL_RX port zero to host")
    dut.expect_exact("SERIAL_TX 1")
    dut.expect_exact("DEVICE_RX0 host to port zero")

    # Ports 1 and 2 take their writes, and none of it lands on port 0.
    dut.expect_exact("DEVICE_TX1 1")
    dut.expect_exact("DEVICE_TX2 1")
    dut.expect_exact("SERIAL_PENDING 0")
    dut.expect_exact("SERIAL_RX port zero to host")

    dut.expect_exact("TEST_END ok")
    assert dut.expect_exact(["OK", "NG"]) == b"OK"
