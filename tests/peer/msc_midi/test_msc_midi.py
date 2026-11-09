"""MSC BOT on a RAM disk and two-cable MIDI over a high-speed link."""
import pytest


@pytest.mark.parametrize("sector,staging", [(511, 64), (512, 512)])
def test_msc_midi(dut, peers, sector, staging):
    device = peers["device"]
    try:
        for _ in range(2):
            device.write(f"start {sector} {staging}")
            device.expect_exact(f"STARTED ok=1 rejected={int(staging < 512)} staging=512\r\n", timeout=30)
            dut.write(f"start {sector}")
            dut.expect_exact(f"HOST_READY ok=1 speed=2 blocks=64 size={sector}\r\n", timeout=30)
            dut.write("exercise")
            dut.expect_exact("CLASSES ok=1 writes=3 reads=3 midi=32 bad=0\r\n", timeout=60)
            device.write("verify")
            device.expect_exact("VERIFY ok=1 guards=1 midi=32 bad=0\r\n", timeout=10)
            dut.write("stop")
            dut.expect_exact("STOPPED heap=1\r\n", timeout=30)
            device.write("stop")
            device.expect_exact("STOPPED 1 heap=1\r\n", timeout=30)
    finally:
        dut.write("stop")
        device.write("stop")
