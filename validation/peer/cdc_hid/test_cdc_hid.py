"""CDC ACM echo with line coding and a boot keyboard, composite, over a high-speed link."""

LENGTHS = (1, 64, 129, 511, 512, 513, 4097)


def test_cdc_hid(dut, peers):
    device = peers["device"]
    try:
        for _ in range(3):
            device.write("start")
            device.expect_exact("STARTED 1\r\n", timeout=30)
            dut.write("start")
            dut.expect_exact("HOST_READY ok=1 speed=2\r\n", timeout=30)
            device.write("coding")
            device.expect_exact("CODING baud=230400\r\n", timeout=5)
            dut.write("serial")
            dut.expect_exact(f"SERIAL ok=1 exchanges={len(LENGTHS)} bytes={sum(LENGTHS)}\r\n", timeout=60)
            device.write("echoed")
            device.expect_exact(f"ECHOED {sum(LENGTHS)} bad=0\r\n", timeout=5)
            device.write("type 3")
            device.expect_exact("TYPED ok=1\r\n", timeout=10)
            dut.write("keys 3")
            dut.expect_exact("KEYS press=3 release=3\r\n", timeout=10)
            dut.write("stop")
            dut.expect_exact("STOPPED heap=1\r\n", timeout=30)
            device.write("stop")
            device.expect_exact("STOPPED 1 heap=1\r\n", timeout=30)
    finally:
        dut.write("stop")
        device.write("stop")
