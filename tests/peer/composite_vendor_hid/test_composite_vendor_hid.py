"""Bulk Vendor + HID keyboard in one device, HID registered second.

composite_hid_vendor covers the same pair with the keyboard registered first.
The descriptors are identical either way, so this module is not about
enumeration - it is about the library resolving TinyUSB's HID instance to the
right class when that class is not the first one registered. Before the fix the
device enumerated fine and the keyboard was dead: no report descriptor, no
reports.

Same shape as composite_hid_vendor: one test, cases in a list, order not
load-bearing.
"""


def _enumeration(dut, device):
    dut.write("e")
    dut.expect(
        r"HOST_ENUM pid=4026 ifcount=\d+ eps=\d+ dup=0 hid=[1-9]\d* vendor=1 ifnumdup=0 claimok=1"
    )


def _keyboard(dut, device):
    """The check that failed before the fix.

    The host only reports KEY once it has parsed the report descriptor and
    received an input report, which are exactly the two paths the instance
    lookup broke.
    """
    device.write("k")
    device.expect_exact("DEVICE_KEY 1")
    dut.expect_exact("KEY a")


def _vendor(dut, device):
    dut.write("v")
    dut.expect_exact("VENDOR_ECHO ok=1 data=echo:ping")

    device.write("q")
    device.expect(r"DEVICE_VENDOR_STATE onrx=[1-9]\d* rxtotal=[1-9]\d* avail=\d+")


def test_composite_vendor_hid(dut, peers):
    device = peers["device"]

    device.write("b")
    device.expect_exact("DEVICE_BEGIN ok ESP_OK")
    device.write("?")
    device.expect_exact("DEVICE_READY 1")

    for check in (_enumeration, _keyboard, _vendor):
        check(dut, device)
