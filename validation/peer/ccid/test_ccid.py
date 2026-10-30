"""Simulated card over a high-speed link: ATR, APDU and Escape across packet boundaries, slot status."""
import pytest


@pytest.mark.parametrize("removable", [0, 1])
def test_ccid(dut, peers, removable):
    device = peers["device"]
    try:
        for _ in range(2):
            device.write(f"start {removable}")
            device.expect_exact("STARTED ok=1 capacity=1024\r\n", timeout=30)
            dut.write("start")
            dut.expect_exact("HOST_READY ok=1 speed=2\r\n", timeout=30)
            dut.write("exercise")
            dut.expect_exact("CCID ok=1 apdu=11 escape=4\r\n", timeout=60)
            device.write("card")
            device.expect_exact("CARD powered=1 apdu=11 escape=4\r\n", timeout=5)
            dut.write("off")
            dut.expect_exact("OFF 1\r\n", timeout=10)
            device.write("card")
            device.expect_exact("CARD powered=0 apdu=11 escape=4\r\n", timeout=5)
            device.write("remove")
            if removable:
                device.expect_exact("REMOVE err=0\r\n", timeout=5)
                dut.write("status")
                dut.expect_exact("STATUS ok=1 present=0 active=0\r\n", timeout=10)
                device.write("insert")
                device.expect_exact("INSERT 1\r\n", timeout=5)
                dut.write("status")
                dut.expect_exact("STATUS ok=1 present=1 active=0\r\n", timeout=10)
            else:
                device.expect_exact("REMOVE err=259\r\n", timeout=5)  # ESP_ERR_INVALID_STATE
            dut.write("stop")
            dut.expect_exact("STOPPED heap=1\r\n", timeout=30)
            device.write("stop")
            device.expect_exact("STOPPED 1 capacity=0 heap=1\r\n", timeout=30)
    finally:
        dut.write("stop")
        device.write("stop")
