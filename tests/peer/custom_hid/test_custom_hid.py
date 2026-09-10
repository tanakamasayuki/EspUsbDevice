"""A sketch-supplied HID report descriptor, across two boards.

The device publishes a report descriptor of its own rather than one of the
library's canned ones, and the host has to fetch it and deliver the input
reports it describes.

The two preconditions are asked rather than awaited. This module was already a
single test, so its boot-banner waits worked - but only because it was first by
construction. The host's report-descriptor line is now a command ('D') as well as
a connect-time announcement, so nothing here depends on having seen boot.
"""


def _report_descriptor(dut, device):
    """The exact descriptor the sketch supplied, as the host fetched it back.

    38 bytes starting 0x05 (Usage Page) and ending 0xc0 (End Collection): a
    truncated or padded fetch would still enumerate and still deliver reports,
    just not the ones the sketch described.
    """
    dut.write("D")
    dut.expect_exact("HID_REPORT iface=0 reported=38 len=38 first=05 last=c0")


def _input_report(dut, device):
    device.write("a")
    device.expect_exact("CMD a 1")
    dut.expect_exact("CUSTOM len=8 data=017f812233445566")


def test_custom_hid(dut, peers):
    device = peers["device"]

    device.write("?")
    device.expect_exact("DEVICE_READY 1")
    dut.write("?")
    dut.expect_exact("HOST_READY 1 vid=303a pid=4004")

    for check in (_report_descriptor, _input_report):
        check(dut, device)
