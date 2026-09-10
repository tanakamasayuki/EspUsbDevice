"""HID keyboard + CDC ACM in one device, across two boards.

One test rather than four, and the reason is in the sketches rather than here.
The four it replaced all depended on the first one having waited for
`HOST_CONNECTED`, a line the host prints once when it enumerates the device. A
test that is not first never sees that line, so running the module in reverse
failed on the very test that had been doing the waiting.

Both sketches now wait instead of announcing: the device blocks until
`device.ready()` (tud_mounted, so the host has completed SET_CONFIGURATION) and
the host blocks until its `onDeviceConnected` has latched an address, each at the
top of its command handler. Neither side has to be asked first, so nothing here
depends on position. `tests/peer/usb_msc` has had that shape all along and was
the only peer module that survived the reverse-order check.

The cases are named functions driven from a list, so a failure names the function
it happened in. Each drives a different class, so the order is not load-bearing.
"""


def _enumeration(dut, device):
    """Both classes present and claimed, no duplicate endpoint address.

    A composite endpoint-allocation collision would surface here as dup=1 or
    claimok=0. The pid is asserted in the same line, which is what the old
    HOST_CONNECTED wait was really checking - that we are talking to our own
    device rather than the S3's transient USB-Serial/JTAG.
    """
    dut.write("e")
    dut.expect(
        r"HOST_ENUM pid=4020 ifcount=\d+ eps=\d+ dup=0 hid=[1-9]\d* cdc=[1-9]\d* claimok=1"
    )


def _keyboard(dut, device):
    device.write("k")
    device.expect_exact("DEVICE_KEY 1")
    dut.expect_exact("KEY a")


def _serial_device_to_host(dut, device):
    device.write("d")
    device.expect_exact("DEVICE_TX 1")
    dut.expect_exact("SERIAL_RX device to host")


def _serial_host_to_device(dut, device):
    dut.write("h")
    dut.expect_exact("SERIAL_TX 1")
    device.expect_exact("DEVICE_RX host to serial")


def test_composite_hid_cdc(dut, peers):
    device = peers["device"]

    # begin() status over UART, independent of USB enumeration: this says both
    # classes registered without hitting the MAX_CLASSES guard.
    device.write("b")
    device.expect_exact("DEVICE_BEGIN ok ESP_OK")

    # The precondition, asked rather than awaited. The device answers only once
    # the host has configured it, so this both waits and asserts.
    device.write("?")
    device.expect_exact("DEVICE_READY 1")

    for check in (_enumeration, _keyboard, _serial_device_to_host, _serial_host_to_device):
        check(dut, device)
