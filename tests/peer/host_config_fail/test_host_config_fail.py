"""A Device that answers SET_CONFIGURATION with STALL: the Host treats it as a
failed enumeration, without a panic, and starts and stops again beside it."""


def test_host_config_fail(dut, peers):
    device = peers["device"]
    try:
        device.write("start")
        device.expect_exact("STARTED begun=1\r\n", timeout=30)
        for _ in range(3):
            # The Device is addressed but never configured, so it is not reported.
            dut.write("start")
            dut.expect_exact("HOST_READY started=1 connected=0\r\n", timeout=30)
            device.write("state")
            device.expect_exact("STATE ready=0\r\n", timeout=5)
            # end() with the failed Device still attached.
            dut.write("stop")
            dut.expect_exact("STOPPED heap=1\r\n", timeout=30)
    finally:
        dut.write("stop")
        dut.expect_exact("STOPPED heap=1\r\n", timeout=30)
        device.write("stop")
        device.expect_exact("STOPPED 0\r\n", timeout=30)
