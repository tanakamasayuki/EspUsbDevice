"""EspUsbDeviceMscFirmwareDisk, driven through its own block callbacks.

No USB host: the sketch calls the read/write entry points the MSC class would
call, which is what lets the whole update path - geometry, image detection,
sequential-order enforcement, the directory entry that says how long the file
is, and the verification that refuses a bad image - be asserted on one board.

The `esp_ota_ops` error lines the peer logs are the refusals this test is about
and are registered in ``tests/conftest.py``.
"""


def test_msc_firmware_disk(dut):
    dut.expect_exact("TEST_BEGIN msc_firmware_disk")
    dut.expect_exact("TEST_END")
    assert dut.expect_exact(["OK", "NG"]) == b"OK"
