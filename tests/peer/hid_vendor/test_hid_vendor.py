"""HID with a vendor-defined usage page, in all three report directions.

Input (device to host), Feature (host to device, through the control pipe) and
Output (host to device) on a vendor page, which is how a sketch carries arbitrary
bytes over HID rather than as keystrokes.

The preconditions are asked rather than awaited: the boot banners this used to
read are printed once and only ever worked for a test that ran first.

The cases are named functions driven from a list; each direction is independent,
so the order is not load-bearing.
"""


def _input_report(dut, device):
    device.write("h")
    device.expect_exact("CMD h 1")
    dut.expect_exact("VENDOR hello vendor")


def _feature_report(dut, device):
    """Feature reports travel on the control pipe and carry the report ID, so a
    device that ignores the ID answers the wrong report."""
    dut.write("f")
    dut.expect_exact("SEND_FEATURE 1")
    device.expect_exact("DEVICE_FEATURE id=6 len=63 host feature")


def _output_report(dut, device):
    dut.write("o")
    dut.expect_exact("SEND_OUTPUT 1")
    device.expect_exact("DEVICE_OUTPUT")


def test_hid_vendor(dut, peers):
    device = peers["device"]

    device.write("?")
    device.expect_exact("DEVICE_READY 1")
    dut.write("?")
    dut.expect_exact("HOST_READY 1 vid=303a pid=4005")

    for check in (_input_report, _feature_report, _output_report):
        check(dut, device)
