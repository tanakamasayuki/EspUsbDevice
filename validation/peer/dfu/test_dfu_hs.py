"""DFU EP0 download, manifest, abort and runtime detach over a high-speed link, RAM backend."""


def test_dfu(dut, peers):
    device = peers["device"]
    try:
        for _ in range(2):
            device.write("start")
            device.expect_exact("STARTED ok=1 capacity=1024\r\n", timeout=30)
            dut.write("start")
            dut.expect_exact("HOST_READY ok=1 speed=2 ep0=64\r\n", timeout=30)  # usb_speed_t 2: high
            dut.write("exercise")
            dut.expect_exact("DFU ok=1\r\n", timeout=60)
            device.write("verify")
            device.expect_exact("VERIFY ok=1 blocks=10 bytes=2177 manifests=1 aborted=1 bad=0 detach=1\r\n", timeout=10)
            dut.write("stop")
            dut.expect_exact("STOPPED 1 heap=1\r\n", timeout=30)
            device.write("stop")
            device.expect_exact("STOPPED 1 capacity=0 heap=1\r\n", timeout=30)
    finally:
        dut.write("stop")
        device.write("stop")
