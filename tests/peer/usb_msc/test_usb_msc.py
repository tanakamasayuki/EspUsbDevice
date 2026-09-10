"""Mass storage across two boards: SCSI commands, transfers, and the error paths.

This module is where the shape the rest of tests/peer now uses came from. Its
host sketch has always waited for the device (`waitForMsc()` at the top of its
command handler) rather than announcing the connection once at boot, which is why
it was the only peer module that survived being run in reverse while the others
failed on a banner they had not been first to read.

One test rather than eighteen, in that same shape: named functions driven from a
list, so a failure names the case it happened in. Every case here is a single
command and its answer, establishing nothing and leaving nothing behind, which
makes this module the honest reverse-order canary - reverse the tuple and it must
still pass.
"""


def _capacity(dut, device):
    dut.write("c")
    dut.expect_exact("MSC_CAPACITY ok=1 blocks=16 block_size=512")


def _block_device_info(dut, device):
    dut.write("d")
    dut.expect("MSC_BLOCK_DEVICE ok=1 addr=\\d+ iface=\\d+ lun=0 max_lun=0 blocks=16 block_size=512 bytes=8192")


def _capacity64(dut, device):
    dut.write("C")
    dut.expect_exact("MSC_CAPACITY64 ok=1 blocks=16 block_size=512")


def _inquiry(dut, device):
    dut.write("i")
    dut.expect_exact("MSC_INQUIRY ok=1 removable=1 vendor='ESP32' product='MSC_PEER' revision='1.0'")


def _max_lun(dut, device):
    dut.write("l")
    dut.expect_exact("MSC_MAX_LUN ok=1 max_lun=0")


def _select_lun(dut, device):
    dut.write("L")
    dut.expect_exact("MSC_SELECT_LUN ok=1")


def _request_sense(dut, device):
    dut.write("s")
    dut.expect_exact("MSC_SENSE ok=1 response=0x70 key=0x00 asc=0x00 ascq=0x00")
    dut.write("S")
    dut.expect_exact("MSC_LAST_SENSE ok=1 response=0x70 key=0x00 asc=0x00 ascq=0x00")


def _test_unit_ready(dut, device):
    dut.write("t")
    dut.expect_exact("MSC_TEST_UNIT_READY ok=1")


def _wait_ready(dut, device):
    dut.write("T")
    dut.expect_exact("MSC_WAIT_READY ok=1")


def _synchronize_cache(dut, device):
    dut.write("y")
    dut.expect_exact("MSC_SYNC_CACHE ok=1")


def _read_boot_block(dut, device):
    dut.write("r")
    dut.expect_exact("MSC_READ ok=1 b0=eb b1=3c b510=55 b511=aa")


def _read64_boot_block(dut, device):
    dut.write("R")
    dut.expect_exact("MSC_READ64 ok=1 b0=eb b1=3c b510=55 b511=aa")


def _write_read_block(dut, device):
    dut.write("w")
    device.expect_exact("DEVICE_WRITE lba=4 offset=0 size=512")
    dut.expect_exact("MSC_WRITE_READ write=1 read=1 b0=a5 b1=a4 b255=5a b511=5a")


def _write_read64_block(dut, device):
    dut.write("W")
    device.expect_exact("DEVICE_WRITE lba=5 offset=0 size=512")
    dut.expect_exact("MSC_WRITE_READ64 write=1 read=1 b0=5a b1=5b b255=a5 b511=a5")


def _multi_block_write_read(dut, device):
    dut.write("m")
    device.expect_exact("DEVICE_WRITE lba=6 offset=0 size=512")
    device.expect_exact("DEVICE_WRITE lba=7 offset=0 size=512")
    dut.expect_exact("MSC_MULTI write=1 read=1 b0=31 b511=30 b512=31 b1023=30")


def _chunked_write_read(dut, device):
    dut.write("g")
    dut.expect_exact("MSC_CHUNKED write=1 read=1 b0=17 b4095=14 b4096=17 b4607=14")


def _out_of_range_is_rejected(dut, device):
    dut.write("o")
    dut.expect_exact("MSC_OUT_OF_RANGE read=0 write=0")


def _failed_write_is_reported(dut, device):
    device.write("F")
    device.expect_exact("DEVICE_FAIL_NEXT_WRITE armed=1")
    dut.write("e")
    device.expect_exact("DEVICE_WRITE_FAIL lba=10 offset=0 size=512")
    dut.expect_exact("MSC_FAILED_WRITE write=0")


def test_usb_msc(dut, peers):
    device = peers["device"]

    # The precondition, asked rather than awaited: the device answers only once
    # the host has configured it.
    device.write("?")
    device.expect_exact("DEVICE_READY 1")

    checks = (
        _capacity,
        _block_device_info,
        _capacity64,
        _inquiry,
        _max_lun,
        _select_lun,
        _request_sense,
        _test_unit_ready,
        _wait_ready,
        _synchronize_cache,
        _read_boot_block,
        _read64_boot_block,
        _write_read_block,
        _write_read64_block,
        _multi_block_write_read,
        _chunked_write_read,
        _out_of_range_is_rejected,
        _failed_write_is_reported,
    )
    for check in checks:
        check(dut, device)
