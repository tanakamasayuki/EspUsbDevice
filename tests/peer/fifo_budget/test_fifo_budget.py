"""High-speed bulk IN FIFO budget: five Vendors fit and enumerate, a sixth fails
begin(); a Function's own bulkInBuffering doubles its endpoint where the
device-wide all-or-nothing Auto cannot."""


def test_fifo_budget(dut, peers):
    device = peers["device"]
    try:
        device.write("start 5")
        device.expect_exact("STARTED begun=1 error=ESP_OK\r\n", timeout=30)
        dut.write("start")
        dut.expect_exact("HOST_READY started=1 connected=1\r\n", timeout=30)
        dut.write("stop")
        dut.expect_exact("STOPPED heap=1\r\n", timeout=30)
        # Six bulk IN packets of 512 bytes, EP0 and the receive FIFO exceed the
        # 4 KiB FIFO: refused before the bus is touched, not at enumeration.
        device.write("start 6")
        device.expect_exact("STARTED begun=0 error=ESP_ERR_INVALID_SIZE\r\n", timeout=30)

        # A Vendor and three CDC ports: four bulk IN endpoints are too many to
        # double them all, so the device-wide Auto doubles none.
        device.write("mix 1 3 0 0")
        device.expect_exact("MIX begun=1 error=ESP_OK mask=0x0\r\n", timeout=30)
        # The Vendor asking for Double gets it alone, and the device enumerates.
        device.write("mix 1 3 2 0")
        device.expect_exact("MIX begun=1 error=ESP_OK mask=0x2\r\n", timeout=30)
        dut.write("start")
        dut.expect_exact("HOST_READY started=1 connected=1\r\n", timeout=30)
        dut.write("stop")
        dut.expect_exact("STOPPED heap=1\r\n", timeout=30)
        # A Function's Single wins over the device's Double.
        device.write("mix 1 0 1 2")
        device.expect_exact("MIX begun=1 error=ESP_OK mask=0x0\r\n", timeout=30)
        # Required doubling that cannot fit is refused, not silently dropped.
        device.write("mix 5 0 2 0")
        device.expect_exact("MIX begun=0 error=ESP_ERR_INVALID_SIZE mask=0x0\r\n", timeout=30)
    finally:
        dut.write("stop")
        dut.expect_exact("STOPPED heap=1\r\n", timeout=30)
        device.write("stop")
        device.expect_exact("STOPPED heap=1\r\n", timeout=30)
