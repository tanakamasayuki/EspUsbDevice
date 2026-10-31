"""High-speed bulk IN FIFO budget: five Vendors fit and enumerate, a sixth fails begin()."""


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
    finally:
        dut.write("stop")
        dut.expect_exact("STOPPED heap=1\r\n", timeout=30)
        device.write("stop")
        device.expect_exact("STOPPED heap=1\r\n", timeout=30)
