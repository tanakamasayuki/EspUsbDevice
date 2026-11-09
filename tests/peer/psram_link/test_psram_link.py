"""MSC medium and Vendor rings in the Device's PSRAM, exercised over a high-speed link."""

ECHO_BYTES = 4 * (1 + 511 + 512 + 513 + 4095 + 4096 + 4097 + 8192 + 16384 + 32768)


def test_psram_link(dut, peers):
    device = peers["device"]
    try:
        for _ in range(2):
            device.write("start")
            started = device.expect(r"STARTED ok=1 external=1 psram=(\d+)\r\n", timeout=30)
            assert int(started.group(1)) >= 8 << 20
            dut.write("start")
            dut.expect_exact("HOST_READY ok=1 speed=2 blocks=8192 size=512\r\n", timeout=30)
            dut.write("exercise")
            dut.expect_exact("PSRAM_LINK ok=1 blocks=85 echoes=40\r\n", timeout=120)
            device.write("verify")
            device.expect_exact(f"VERIFY ok=1 guards=1 echoed={ECHO_BYTES} bad=0\r\n", timeout=60)
            dut.write("stop")
            dut.expect_exact("STOPPED heap=1\r\n", timeout=30)
            device.write("stop")
            device.expect_exact("STOPPED 1 guards=1 heap=1\r\n", timeout=30)
    finally:
        dut.write("stop")
        device.write("stop")
