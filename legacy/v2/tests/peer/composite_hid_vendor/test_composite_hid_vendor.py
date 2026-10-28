"""HID keyboard + bulk Vendor in one device, across two boards.

One test rather than three, for the reason in tests/peer/composite_hid_cdc: the
first test read a connect banner the other two relied on having been read.

The cases are named functions driven from a list, each driving a different
class, so the order is not load-bearing.
"""


def _enumeration(dut, device):
    """HID and Vendor both present and claimed, with distinct interface numbers.

    Before the descriptor duplication fix the Vendor interface was emitted twice
    - once inside the HID blob, once by the core's vendor loader - which shows up
    as dup=1 / ifnumdup=1 / claimok=0, or as a failed enumeration.
    """
    dut.write("e")
    dut.expect(
        r"HOST_ENUM pid=4024 ifcount=\d+ eps=\d+ dup=0 hid=[1-9]\d* vendor=1 ifnumdup=0 claimok=1"
    )


def _keyboard(dut, device):
    device.write("k")
    device.expect_exact("DEVICE_KEY 1")
    dut.expect_exact("KEY a")


def _vendor(dut, device):
    """Bulk Vendor round-trip driven entirely by the onRx callback.

    rxtotal counts bytes received through the callback, so a non-zero value is
    what says the callback fired rather than polling having covered for it.
    """
    dut.write("v")
    dut.expect_exact("VENDOR_ECHO ok=1 data=echo:ping")

    device.write("q")
    device.expect(r"DEVICE_VENDOR_STATE onrx=[1-9]\d* rxtotal=[1-9]\d* avail=\d+")


def test_composite_hid_vendor(dut, peers):
    device = peers["device"]

    device.write("b")
    device.expect_exact("DEVICE_BEGIN ok ESP_OK")
    device.write("?")
    device.expect_exact("DEVICE_READY 1")

    for check in (_enumeration, _keyboard, _vendor):
        check(dut, device)
