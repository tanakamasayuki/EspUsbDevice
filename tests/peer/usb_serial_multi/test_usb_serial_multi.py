"""Two CDC ACM ports on one device, each driven individually.

One test rather than seven. Six of the seven failed when run on their own: five
wrote to the host without waiting for enumeration, which only works while an
earlier test has already waited, and the separation check counted receives that
the two preceding tests had produced.

The cases are named functions driven from a list. `_ports_are_separate` reads
counters that `_port0` and `_port1` produce, so those three are one ordered
group rather than independent cases, and this module is not a candidate for a
reversed check list.
"""


def _device_reports_two_ports(dut, device):
    """Two ACM functions is the S3 ceiling, and each object drives the instance
    matching its position in the configuration descriptor."""
    device.write("p")
    device.expect_exact("DEVICE_PORTS max=2 port0=0 port1=1")


def _host_enumeration(dut, device):
    """Four interfaces (control + data per port), six endpoints, no address
    collision, every claim accepted."""
    dut.write("e")
    dut.expect_exact("HOST_ENUM pid=4015 ifcount=4 eps=6 dup=0 ctrl=2 data=2 claimok=1")


def _iad_device_class(dut, device):
    """A configuration carrying interface associations has to say so at the
    device level (0xEF/0x02/0x01), which is what makes a host load a composite
    parent driver and bind per function rather than one driver across all four
    interfaces."""
    dut.write("k")
    dut.expect_exact("HOST_CLASS class=ef sub=02 proto=01")


def _port_mapping(dut, device):
    """Host and device agree on which function is which port, down to the
    interface and endpoint numbers, and both ports carry data."""
    dut.write("p")
    dut.expect_exact("HOST_PORTS count=2")
    dut.expect_exact("HOST_PORT 0 ctrl=0 data=1 in=82 out=02 ready=1")
    dut.expect_exact("HOST_PORT 1 ctrl=2 data=3 in=84 out=04 ready=1")


def _port0_both_ways(dut, device):
    dut.write("h")
    dut.expect_exact("SERIAL_TX0 1")
    device.expect_exact("DEVICE_RX0 host to port zero")

    device.write("d")
    device.expect_exact("DEVICE_TX0 1")
    dut.expect_exact("SERIAL_RX0 from port zero")


def _port1_both_ways(dut, device):
    dut.write("H")
    dut.expect_exact("SERIAL_TX1 1")
    device.expect_exact("DEVICE_RX1 host to port one")

    device.write("D")
    device.expect_exact("DEVICE_TX1 1")
    dut.expect_exact("SERIAL_RX1 from port one")


def _ports_are_separate(dut, device):
    """Each port received only what was addressed to it.

    Reads the counters the two exchanges above produced, so it belongs after
    them: one message each, and nothing left over on either host-side stream.
    """
    device.write("r")
    device.expect_exact("DEVICE_RXCOUNT port0=1 port1=1")
    dut.write("q")
    dut.expect_exact("SERIAL_PENDING p0=0 p1=0")


def _line_coding_is_per_port(dut, device):
    """SET_LINE_CODING goes to one port's own control interface.

    Both ports came up at the host's 115200; changing port 1 must leave port 0
    alone, which is the control path being per-port rather than only the data
    path.
    """
    dut.write("b")
    dut.expect_exact("SERIAL_BAUD1 1")
    device.write("l")
    device.expect_exact("DEVICE_LINE_CODING port0=115200 port1=57600")


def test_usb_serial_multi(dut, peers):
    device = peers["device"]

    device.write("b")
    device.expect_exact("DEVICE_BEGIN ok ESP_OK")
    # The precondition, asked rather than awaited: begun=1 is this sketch's own
    # setup having run, the leading flag is the host having configured it.
    device.write("?")
    device.expect_exact("DEVICE_READY 1 begun=1")

    checks = (
        _device_reports_two_ports,
        _host_enumeration,
        _iad_device_class,
        _port_mapping,
        _port0_both_ways,
        _port1_both_ways,
        _ports_are_separate,
        _line_coding_is_per_port,
    )
    for check in checks:
        check(dut, device)
