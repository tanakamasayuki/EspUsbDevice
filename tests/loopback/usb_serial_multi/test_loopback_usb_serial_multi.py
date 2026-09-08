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
    # duplicate address. This is the six-IN-endpoint allocation on the device's
    # HS controller actually succeeding, not just a well-formed descriptor.
    dut.expect_exact("HOST_ENUM ifcount=6 ctrl=3 data=3 eps=9 bulk=6 intr=3 dup=0")
    dut.expect_exact("HOST_CLASS class=ef sub=02 proto=01")

    # Host and device agree on which function is which port, down to the
    # interface and endpoint numbers. Three ports fit the host's full-speed
    # controller only because a port costs two channels, not three: EP0 plus
    # 3 x (bulk IN + bulk OUT) is seven of eight.
    dut.expect_exact("HOST_PORTS count=3")
    dut.expect_exact("HOST_PORT 0 ctrl=0 data=1 in=82 out=02 ready=1")
    dut.expect_exact("HOST_PORT 1 ctrl=2 data=3 in=84 out=04 ready=1")
    dut.expect_exact("HOST_PORT 2 ctrl=4 data=5 in=86 out=06 ready=1")

    # Every port carries data both ways, one port at a time.
    for port in range(3):
        dut.expect_exact(f"DEVICE_TX{port} 1")
        dut.expect_exact(f"SERIAL_RX{port} port {port} to host")
        dut.expect_exact(f"SERIAL_TX{port} 1")
        dut.expect_exact(f"DEVICE_RX{port} host to port {port}")

    # Each exchange was matched exactly, so anything left over would be traffic
    # that reached a port it was not addressed to.
    dut.expect_exact("PENDING host=0 device=0")

    # SET_LINE_CODING carries one port's own control interface in wIndex, so
    # this is the control path being per-port too - and the host never claimed
    # those interfaces, the request goes over EP0.
    dut.expect_exact("SERIAL_BAUD2 1")
    dut.expect_exact("DEVICE_LINE_CODING p0=115200 p1=115200 p2=57600")

    dut.expect_exact("TEST_END ok")
    assert dut.expect_exact(["OK", "NG"]) == b"OK"
