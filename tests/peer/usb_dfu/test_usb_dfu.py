"""DFU peer test.

DUT = the USB host (EspUsbHost, ``usb_dfu.ino``); the peer = an EspUsbDevice
keyboard with an ``EspUsbDeviceDfu`` function beside it (``peer_device/``).

Nothing here downloads a real image. A valid one would reflash the peer board and
take the test sketch with it, so the two cases are the two ways an image is
refused - a bad magic byte, which ``esp_ota_write()`` rejects on the first block,
and a well-formed-looking block that fails verification at
``esp_ota_end()``. Between them they walk the whole path: the interface is
claimed, DNLOAD data reaches flash, a failure surfaces as a DFU status rather
than a stall, CLRSTATUS recovers, and the boot partition never moves.

The two ``E (...) esp_ota_ops`` lines the peer logs are those refusals and are
registered in ``tests/conftest.py``.
"""

# DFU 1.1 state numbers (dfu.h). Named because the assertions below are about
# the state machine, not about integers.
DFU_IDLE = 2
DFU_DNLOAD_IDLE = 5
DFU_ERROR = 10

# DFU 1.1 status numbers.
DFU_STATUS_OK = 0
DFU_STATUS_ERR_WRITE = 3
DFU_STATUS_ERR_VERIFY = 7


def _enumeration(dut):
    """The DFU interface must appear beside the keyboard and cost no endpoints."""
    dut.write("i")
    dut.expect_exact("INTERFACE number=0 class=0x03 subclass=0x01 protocol=0x01 endpoints=2")
    dut.expect_exact("INTERFACE number=1 class=0xfe subclass=0x01 protocol=0x02 endpoints=0")
    # The keyboard keeps both of its endpoints, and DFU adds none.
    dut.expect_exact("ENDPOINT iface=0 ep=0x01 attrs=0x03 mps=8")
    dut.expect_exact("ENDPOINT iface=0 ep=0x81 attrs=0x03 mps=8")
    dut.expect_exact("DFU_ENUM interface=1 protocol=2 endpoints=0 interfaces=2")


def _functional_descriptor(dut, device):
    """Read back from the wire, not from the host driver's parsed view."""
    device.write("t")
    transfer = int(device.expect(r"DEVICE_XFER (\d+)", timeout=10).group(1))

    dut.write("f")
    match = dut.expect(
        r"DFU_FUNCTIONAL ok=1 attrs=0x(\w+) timeout=(\d+) transfer=(\d+) bcd=0x(\w+) total=\d+",
        timeout=10,
    )
    attrs = int(match.group(1), 16)
    assert attrs & 0x01, "bitCanDnload must be set"
    # Download mode must not claim manifestation tolerance: the device restarts
    # into the new image, so the host has to expect it to disappear.
    assert not attrs & 0x04, "bitManifestationTolerant must be clear"
    assert int(match.group(2)) == 1000
    assert int(match.group(3)) == transfer, "wTransferSize must be CFG_TUD_DFU_XFER_BUFSIZE"
    assert int(match.group(4), 16) == 0x0110, "DFU 1.1"


def _idle_at_rest(dut):
    dut.write("s")
    match = dut.expect(r"DFU_STATUS ok=1 status=(\d+) state=(\d+) poll=\d+", timeout=10)
    assert int(match.group(1)) == DFU_STATUS_OK
    assert int(match.group(2)) == DFU_IDLE


def _bad_magic_is_refused(dut, device):
    """A block that is not an ESP image must be refused on the first write.

    ``esp_ota_write()`` checks the image magic before it accepts anything, so the
    device never writes a byte of it. What the host must see is a DFU status, not
    a stalled control transfer.
    """
    device.write("z")
    device.expect_exact("DEVICE_RESET")

    dut.write("1")
    match = dut.expect(r"DFU_DNLOAD sent=1 block=0 len=64 ok=1 status=(\d+) state=(\d+)", timeout=20)
    assert int(match.group(1)) == DFU_STATUS_ERR_WRITE
    assert int(match.group(2)) == DFU_ERROR

    device.write("s")
    match = device.expect(
        r"DEVICE_DFU blocks=(\d+) bytes=(\d+) errors=(\d+) complete=(\d+) status=(\d+)",
        timeout=10,
    )
    assert int(match.group(1)) == 0, "nothing was accepted"
    assert int(match.group(3)) == 1, "one error reported"
    assert int(match.group(5)) == DFU_STATUS_ERR_WRITE

    # CLRSTATUS is the documented way out of dfuERROR, and it has to work or the
    # device would need a replug after every mistyped file.
    dut.write("c")
    match = dut.expect(r"DFU_CLRSTATUS sent=1 status=(\d+) state=(\d+)", timeout=10)
    assert int(match.group(1)) == DFU_STATUS_OK
    assert int(match.group(2)) == DFU_IDLE


def _download_then_failed_verification(dut, device):
    """Blocks that start with the image magic are written, then rejected at the end.

    The payload is plausible enough for ``esp_ota_write()`` and nonsense to
    ``esp_ota_end()``, which is what puts the verification step under test:
    DFU must report the failure as ERR_VERIFY and leave the boot partition alone.
    """
    device.write("z")
    device.expect_exact("DEVICE_RESET")

    for command, block in (("2", 0), ("3", 1)):
        dut.write(command)
        match = dut.expect(
            rf"DFU_DNLOAD sent=1 block={block} len=256 ok=1 status=(\d+) state=(\d+)",
            timeout=20,
        )
        assert int(match.group(1)) == DFU_STATUS_OK
        assert int(match.group(2)) == DFU_DNLOAD_IDLE

    device.write("s")
    match = device.expect(
        r"DEVICE_DFU blocks=(\d+) bytes=(\d+) errors=(\d+) complete=(\d+) status=(\d+)",
        timeout=10,
    )
    assert int(match.group(1)) == 2, "both blocks reached the sketch"
    assert int(match.group(2)) == 512, "every byte reached flash"
    assert int(match.group(3)) == 0, "no error yet"

    dut.write("m")
    match = dut.expect(r"DFU_MANIFEST sent=1 ok=1 status=(\d+) state=(\d+)", timeout=30)
    assert int(match.group(1)) == DFU_STATUS_ERR_VERIFY
    assert int(match.group(2)) == DFU_ERROR

    device.write("s")
    match = device.expect(
        r"DEVICE_DFU blocks=(\d+) bytes=(\d+) errors=(\d+) complete=(\d+) status=(\d+)",
        timeout=10,
    )
    assert int(match.group(3)) == 1, "the failed verification is reported once"
    assert int(match.group(4)) == 0, "onComplete never ran for an image that failed"
    assert int(match.group(5)) == DFU_STATUS_ERR_VERIFY


def _boot_partition_unchanged(device):
    """The peer still boots what it is running. This is the assertion that keeps
    the rig alive, and the one a broken commit path would break first."""
    device.write("b")
    match = device.expect(r"DEVICE_BOOT boot=(\w+) running=(\w+)", timeout=10)
    assert match.group(1) == match.group(2), match.group(0)


def _recovers_for_another_download(dut, device):
    """After a failure the function must accept a fresh download from block 0.

    The OTA handle from the failed attempt has to be gone, or begin() would find
    it still open and the device would need a power cycle between attempts.
    """
    dut.write("c")
    dut.expect(r"DFU_CLRSTATUS sent=1 status=0 state=2", timeout=10)

    device.write("z")
    device.expect_exact("DEVICE_RESET")

    dut.write("2")
    match = dut.expect(r"DFU_DNLOAD sent=1 block=0 len=256 ok=1 status=(\d+) state=(\d+)", timeout=20)
    assert int(match.group(1)) == DFU_STATUS_OK
    assert int(match.group(2)) == DFU_DNLOAD_IDLE

    # DFU_ABORT drops it again, which is what dfu-util sends when the user stops.
    dut.write("x")
    match = dut.expect(r"DFU_ABORT sent=1 status=(\d+) state=(\d+)", timeout=10)
    assert int(match.group(1)) == DFU_STATUS_OK
    assert int(match.group(2)) == DFU_IDLE


def test_usb_dfu(dut, peers):
    device = peers["device"]

    device.write("?")
    device.expect_exact("DEVICE_READY 1")

    _enumeration(dut)
    _functional_descriptor(dut, device)
    _idle_at_rest(dut)
    _bad_magic_is_refused(dut, device)
    _download_then_failed_verification(dut, device)
    _boot_partition_unchanged(device)
    _recovers_for_another_download(dut, device)
    _boot_partition_unchanged(device)
