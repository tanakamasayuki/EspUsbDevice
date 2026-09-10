def test_usb_serial_multi(dut, peers):
    """Two CDC ACM ports on one device, each driven individually.

    One test rather than seven. Six of the seven failed when run on their own:
    five wrote to the host without waiting for enumeration, which only works
    while an earlier test has already waited, and the separation check counted
    receives that the two preceding tests had produced. Both problems are the
    same problem - the sequence only makes sense in order - so it is written as
    a sequence.
    """
    device = peers["device"]

    dut.expect_exact("HOST_CONNECTED vid=303a pid=4015")

    device.write("b")
    device.expect_exact("DEVICE_BEGIN ok ESP_OK")
    device.write("?")
    device.expect_exact("DEVICE_READY")

    # Two ACM functions is the S3 ceiling, and each object drives the TinyUSB
    # instance matching its position in the configuration descriptor.
    device.write("p")
    device.expect_exact("DEVICE_PORTS max=2 port0=0 port1=1")

    # Four interfaces (control + data per port), six endpoints, no address
    # collision, every claim accepted.
    dut.write("e")
    dut.expect_exact("HOST_ENUM pid=4015 ifcount=4 eps=6 dup=0 ctrl=2 data=2 claimok=1")

    # A configuration carrying interface associations has to say so at the
    # device level (0xEF/0x02/0x01), which is what makes a host load a composite
    # parent driver and then bind per function instead of binding one driver
    # across all four interfaces.
    dut.write("k")
    dut.expect_exact("HOST_CLASS class=ef sub=02 proto=01")

    # Host and device agree on which function is which port, down to the
    # interface and endpoint numbers: port 0 is interfaces 0/1 with bulk
    # 0x82/0x02, port 1 is interfaces 2/3 with bulk 0x84/0x04. Both carry data.
    dut.write("p")
    dut.expect_exact("HOST_PORTS count=2")
    dut.expect_exact("HOST_PORT 0 ctrl=0 data=1 in=82 out=02 ready=1")
    dut.expect_exact("HOST_PORT 1 ctrl=2 data=3 in=84 out=04 ready=1")

    # Port 0, both directions.
    dut.write("h")
    dut.expect_exact("SERIAL_TX0 1")
    device.expect_exact("DEVICE_RX0 host to port zero")

    device.write("d")
    device.expect_exact("DEVICE_TX0 1")
    dut.expect_exact("SERIAL_RX0 from port zero")

    # Port 1, both directions.
    dut.write("H")
    dut.expect_exact("SERIAL_TX1 1")
    device.expect_exact("DEVICE_RX1 host to port one")

    device.write("D")
    device.expect_exact("DEVICE_TX1 1")
    dut.expect_exact("SERIAL_RX1 from port one")

    # Each port received only what was addressed to it: one message each, and
    # nothing left over on either host-side stream.
    device.write("r")
    device.expect_exact("DEVICE_RXCOUNT port0=1 port1=1")
    dut.write("q")
    dut.expect_exact("SERIAL_PENDING p0=0 p1=0")

    # SET_LINE_CODING goes to one port's own control interface. Both ports came
    # up at the host's 115200; changing port 1 must leave port 0 alone.
    dut.write("b")
    dut.expect_exact("SERIAL_BAUD1 1")
    device.write("l")
    device.expect_exact("DEVICE_LINE_CODING port0=115200 port1=57600")
