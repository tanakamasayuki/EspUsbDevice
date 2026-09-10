"""CDC ACM serial across two boards: both directions and the line coding API.

One test rather than three. The three it replaced shared a single enumeration
wait: only the first waited for HOST_CONNECTED, and the other two wrote to the
host immediately, which works while they run after it and races the enumeration
when either is run on its own.

The cases below are named functions driven from a list, so a failure names the
function it happened in rather than only a line number. `_line_coding` is not a
case in the same sense as the other two: its three steps are one conversation,
and the last of them asserts that a partial request left the earlier fields
alone. Reversing that would assert a state nobody established, so this module is
not a candidate for a reversed check list.
"""


def _device_to_host(dut, device):
    device.write("d")
    device.expect_exact("DEVICE_TX 1")
    dut.expect_exact("SERIAL_RX device to host")


def _host_to_device(dut, device):
    dut.write("h")
    dut.expect_exact("SERIAL_TX 1")
    device.expect_exact("DEVICE_RX host to serial")


def _line_coding(dut, device):
    """SET_LINE_CODING reaches the device, and a partial request keeps the rest.

    Ordered on purpose. The third step sets baud alone and asserts that stop
    bits, parity and data bits still hold what the second step put there, which
    is the property being tested.
    """
    dut.write("c")
    dut.expect_exact("SERIAL_CONFIG 1")
    device.write("l")
    device.expect_exact("DEVICE_LINE_CODING seen=1 baud=57600 stop=2 parity=2 data=7")
    dut.write("h")
    dut.expect_exact("SERIAL_TX 1")
    device.expect_exact("DEVICE_RX host to serial")

    # Mark parity and 1.5 stop bits, the encodings a naive implementation gets
    # wrong.
    dut.write("m")
    dut.expect_exact("SERIAL_CONFIG_MARK 1")
    device.write("l")
    device.expect_exact("DEVICE_LINE_CODING seen=1 baud=300 stop=1 parity=3 data=5")

    # Baud alone: the other fields must survive from the request above.
    dut.write("b")
    dut.expect_exact("SERIAL_BAUD 1")
    device.write("l")
    device.expect_exact("DEVICE_LINE_CODING seen=1 baud=115200 stop=1 parity=3 data=5")


def test_usb_serial(dut, peers):
    device = peers["device"]

    # The preconditions, asked rather than awaited. The device answers only once
    # the host has configured it; the host only once it has enumerated the peer,
    # and the pid says it is our peer rather than a neighbouring board.
    device.write("?")
    device.expect_exact("DEVICE_READY 1")
    dut.write("?")
    dut.expect_exact("HOST_READY 1 vid=303a pid=4016")

    for check in (_device_to_host, _host_to_device, _line_coding):
        check(dut, device)
