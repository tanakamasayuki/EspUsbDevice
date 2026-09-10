"""HID keyboard + MSC in one device, across two boards.

One test rather than three. Only the first waited for enumeration - and it did
so by reading the host's `HOST_CONNECTED` banner, printed once at connect, which
a test that is not first never sees. The other two wrote to the host straight
away, which works only while something ahead of them has already waited.

Both sketches now answer rather than announce; see
tests/peer/composite_hid_cdc for the full note. The cases are named functions
driven from a list, each driving a different class, so the order is not
load-bearing.
"""


def _enumeration(dut, device):
    """Both interface classes claimed, no duplicate endpoint address.

    HID takes EP1 (reserved in the core bitmask) and MSC draws EP2 from the
    allocator, so there is no collision. Regression guard for
    docs/DESIGN_NOTES.ja.md "複合時の endpoint 採番衝突".
    """
    dut.write("e")
    dut.expect(r"HOST_ENUM pid=4021 ifcount=\d+ eps=\d+ dup=0 hid=[1-9]\d* msc=[1-9]\d* claimok=1")


def _keyboard(dut, device):
    device.write("k")
    device.expect_exact("DEVICE_KEY 1")
    dut.expect_exact("KEY a")


def _msc_capacity(dut, device):
    dut.write("m")
    dut.expect_exact("MSC_CAPACITY ok=1 blocks=16 block_size=512")


def test_composite_hid_msc(dut, peers):
    device = peers["device"]

    device.write("b")
    device.expect_exact("DEVICE_BEGIN ok ESP_OK")
    device.write("?")
    device.expect_exact("DEVICE_READY 1")

    for check in (_enumeration, _keyboard, _msc_capacity):
        check(dut, device)
