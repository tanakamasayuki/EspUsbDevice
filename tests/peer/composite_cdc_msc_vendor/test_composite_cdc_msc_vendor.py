"""CDC + MSC + bulk Vendor in one device, each function exercised in turn.

One test rather than four. Only the first of the four waited for enumeration;
the rest wrote to the host straight away, which works while they follow it and
races the enumeration when one is run on its own.

The cases are named functions driven from a list. Each drives a different class
and establishes what it needs, so the order is not load-bearing.
"""


def _enumeration(dut, device):
    """Three non-HID classes all drawing endpoints from the library allocator:
    every interface class present and claimed, no duplicate endpoint address."""
    dut.write("e")
    dut.expect(
        r"HOST_ENUM pid=4023 ifcount=\d+ eps=\d+ dup=0 cdc=[1-9]\d* msc=[1-9]\d* vendor=[1-9]\d* claimok=1"
    )


def _cdc(dut, device):
    device.write("d")
    device.expect_exact("DEVICE_TX 1")
    dut.expect_exact("SERIAL_RX device to host")

    dut.write("h")
    dut.expect_exact("SERIAL_TX 1")
    device.expect_exact("DEVICE_RX host to serial")


def _msc(dut, device):
    dut.write("m")
    dut.expect_exact("MSC_CAPACITY ok=1 blocks=16 block_size=512")


def _vendor(dut, device):
    """Bulk Vendor round-trip driven entirely by the onRx callback.

    Regression guard for the tud_vendor_rx_cb signature/linkage fix: before it
    the library defined a 1-arg tud_vendor_rx_cb that got a C++-mangled symbol
    and never overrode TinyUSB's weak default, so onRx never fired. See
    src/EspUsbDevice.cpp and docs/DESIGN_NOTES.ja.md "複合時の vendor RX callback".
    """
    dut.write("v")
    dut.expect_exact("VENDOR_ECHO ok=1 data=echo:ping")

    # rxtotal counts bytes received through the callback, so a non-zero value is
    # what says the callback fired rather than polling having covered for it.
    device.write("q")
    device.expect(r"DEVICE_VENDOR_STATE onrx=[1-9]\d* rxtotal=[1-9]\d* avail=\d+")


def test_composite_cdc_msc_vendor(dut, peers):
    device = peers["device"]

    device.write("b")
    device.expect_exact("DEVICE_BEGIN ok ESP_OK")
    # The precondition, asked rather than awaited. The device answers only once
    # the host has configured it, so this both waits and asserts; the pid is
    # checked by _enumeration below, from the host's side.
    device.write("?")
    device.expect_exact("DEVICE_READY 1")

    for check in (_enumeration, _cdc, _msc, _vendor):
        check(dut, device)
