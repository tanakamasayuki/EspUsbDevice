"""USBVendor (bulk + control) peer test, EspUsbDevice-repo copy.

DUT = the USB host (EspUsbHost, ``usb_vendor.ino``); the peer = the EspUsbDevice
vendor-specific device (``peer_device/``).

The three tests below the first one were added once the host side moved to
EspUsbHost 2.7.0: they need APIs introduced in 2.5.3 (opened-pipe reporting,
auto ZLP, and the asynchronous bulk-OUT queue) that the previously pinned 2.5.2
did not have, so this device behaviour had no automated coverage.
"""

def test_usb_vendor_enumeration_and_transfer(dut, peers):
    device = peers["device"]

    dut.expect_exact("HOST_CONNECTED")
    device.write("?")
    device.expect_exact("DEVICE_READY")

    dut.write("i")
    dut.expect_exact("INTERFACE number=0 class=0xff subclass=0x00 protocol=0x00 endpoints=2")
    dut.expect_exact("ENDPOINT iface=0 ep=0x01 attrs=0x02 mps=64 interval=0")
    dut.expect_exact("ENDPOINT iface=0 ep=0x81 attrs=0x02 mps=64 interval=0")
    dut.expect_exact("VENDOR_ENUM interface=1 bulk_out=1 bulk_in=1")

    dut.write("o")
    dut.expect_exact("VENDOR_OPEN 1")

    dut.write("w")
    dut.expect_exact("VENDOR_WRITE 1")
    dut.write("p")
    dut.expect_exact("VENDOR_DATA seen=1 data=echo:ping")

    dut.write("r")
    dut.expect_exact("VENDOR_READ len=9 data=echo:ping")

    dut.write("c")
    dut.expect_exact("VENDOR_CONTROL_IN ok=1 len=18 data=EspUsbDeviceVendor")

    dut.write("C")
    dut.expect_exact("VENDOR_CONTROL_OUT 1")

    dut.write("u")
    dut.expect("WEBUSB_URL ok=1 len=[1-9][0-9]* found=1")

    device.write("s")
    device.expect("DEVICE_STATUS rx=4 control=[1-9][0-9]*")


def _reset_device_counters(device):
    """Zero the device's RX counters and silence its echo.

    Echo is the point of the first test, but for a bulk-OUT burst it is only
    backpressure: nothing reads the echoes back, so they would queue on the device
    and change what the RX measurement means.
    """
    device.write("e")
    m = device.expect(r"DEVICE_ECHO (\d)", timeout=5)
    if int(m.group(1)) != 0:
        # Already off (a previous test left it that way): toggle back.
        device.write("e")
        device.expect_exact("DEVICE_ECHO 0")
    device.write("z")
    device.expect_exact("DEVICE_RX_RESET")


def _restore_device_echo(device):
    device.write("e")
    device.expect_exact("DEVICE_ECHO 1")


def test_usb_vendor_opened_pipes(dut, peers):
    """The pipes vendorOpen() actually opened must match what the device declared.

    The enumeration test above reads the descriptor; this reads the driver's view
    (`vendorInEndpoint()` / `vendorOutEndpoint()` / packet sizes, EspUsbHost
    2.5.3). Both being right is what proves the device's endpoint descriptors are
    usable, not merely well-formed - a device that declares 64 but opens as
    something else would pass the descriptor check alone.
    """
    device = peers["device"]
    device.write("?")
    device.expect_exact("DEVICE_READY")

    dut.write("o")
    dut.expect_exact("VENDOR_OPEN 1")

    dut.write("e")
    # EspUsbDeviceVendor declares one bulk pair, full speed, 64-byte packets.
    dut.expect_exact("VENDOR_PIPES in=0x81 out=0x01 in_mps=64 out_mps=64")


def test_usb_vendor_full_packet_write_with_zlp(dut, peers):
    """A transfer that is exactly one full packet must arrive complete.

    A bulk OUT whose length is a non-zero multiple of wMaxPacketSize does not
    terminate the transfer by itself; the host has to follow it with a ZLP, which
    is what `vendorSetAutoZlp()` (2.5.3) does. This is the device-side half of the
    boundary the P4 manual test covers for device-to-host: all 64 bytes must reach
    the sketch, and the endpoint must stay usable afterwards.
    """
    device = peers["device"]
    device.write("?")
    device.expect_exact("DEVICE_READY")
    dut.write("o")
    dut.expect_exact("VENDOR_OPEN 1")

    _reset_device_counters(device)
    try:
        dut.write("z")
        dut.expect_exact("VENDOR_WRITE_MPS ok=1 len=64 zlp=1")

        device.write("b")
        m = device.expect(r"DEVICE_RX_BYTES rx=(\d+) chunks=(\d+) last=(\d+)", timeout=10)
        # Byte count is the assertion that matters: all 64 arrive, and the ZLP adds
        # nothing (the sketch only counts non-empty reads, and a stray extra byte
        # would show up here).
        assert int(m.group(1)) == 64, m.group(0)
        # One packet should surface as one read. This is the tight expectation
        # rather than a guarantee - if TinyUSB ever hands the payload over in two
        # pieces, relax this to `>= 1` and keep the byte-count assertion.
        assert int(m.group(2)) == 1, m.group(0)
        assert int(m.group(3)) == 64, m.group(0)
    finally:
        _restore_device_echo(device)

    # The endpoint still works after the full-packet transfer plus ZLP.
    dut.write("w")
    dut.expect_exact("VENDOR_WRITE 1")
    dut.write("p")
    dut.expect_exact("VENDOR_DATA seen=1 data=echo:ping")


def test_usb_vendor_queued_writes(dut, peers):
    """Back-to-back queued transfers must all reach the device.

    `vendorWriteQueueBegin()` / `vendorWriteAcquire()` / `vendorWriteSubmit()`
    (2.5.3) submit four full packets with no round trip in between, which is the
    pattern that stresses the device's OUT FIFO and `onRx()` handling hardest. With
    the synchronous API available at 2.5.2 the host could not produce it.
    """
    device = peers["device"]
    device.write("?")
    device.expect_exact("DEVICE_READY")
    dut.write("o")
    dut.expect_exact("VENDOR_OPEN 1")

    _reset_device_counters(device)
    try:
        dut.write("q")
        dut.expect_exact("VENDOR_QUEUE ok=1 frames=4 bytes=256 pending=0")

        # Every byte arrives. Chunk count is deliberately not asserted: the device
        # reads whatever the FIFO holds when onRx() runs, so four 64-byte packets
        # may legitimately surface as fewer, larger reads.
        deadline_reads = 20
        received = 0
        for _ in range(deadline_reads):
            device.write("b")
            m = device.expect(r"DEVICE_RX_BYTES rx=(\d+) chunks=(\d+) last=(\d+)", timeout=10)
            received = int(m.group(1))
            if received >= 256:
                break
        assert received == 256, m.group(0)
    finally:
        _restore_device_echo(device)
