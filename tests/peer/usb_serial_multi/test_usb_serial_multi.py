def test_usb_serial_multi_enumerates(dut, peers):
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


def test_usb_serial_multi_declares_iad_composite(dut):
    # A configuration carrying interface associations has to say so at the
    # device level (0xEF/0x02/0x01), which is what makes a host load a
    # composite parent driver and then bind per function instead of binding one
    # driver across all four interfaces.
    dut.write("k")
    dut.expect_exact("HOST_CLASS class=ef sub=02 proto=01")


def test_usb_serial_multi_host_to_first_port(dut, peers):
    device = peers["device"]

    dut.write("h")
    dut.expect_exact("SERIAL_TX 1")
    device.expect_exact("DEVICE_RX0 host to port zero")

    # The bytes went to one port, not to both.
    device.write("r")
    device.expect_exact("DEVICE_RXCOUNT port0=1 port1=0")


def test_usb_serial_multi_first_port_to_host(dut, peers):
    device = peers["device"]

    device.write("d")
    device.expect_exact("DEVICE_TX0 1")
    dut.expect_exact("SERIAL_RX from port zero")


def test_usb_serial_multi_ports_are_separate(dut, peers):
    device = peers["device"]

    # Port 1 accepts the write - its endpoints are open and its FIFO is its
    # own - but the bytes must not surface on the stream bound to port 0.
    device.write("D")
    device.expect_exact("DEVICE_TX1 1")

    dut.write("q")
    dut.expect_exact("SERIAL_PENDING 0")

    # Port 0 still works afterwards, so the silence above was separation and
    # not a stalled pipe.
    device.write("d")
    device.expect_exact("DEVICE_TX0 1")
    dut.expect_exact("SERIAL_RX from port zero")
