"""One P4, two native controllers, a physical FS link in both role mappings."""
import pytest


@pytest.mark.parametrize("controller", ["hs", "fs"])
def test_p4_loopback(dut, controller):
    try:
        for _ in range(3):
            dut.write(f"start {controller}")
            dut.expect_exact(f"START ok=1 device={controller} speed=1 in=64 out=64 error=0\r\n", timeout=40)
            dut.write("exercise")
            dut.expect_exact("EXERCISE ok=1 copies=11 bytes=6210 direct=4 echoed=6210 bad=0 guards=1\r\n", timeout=90)
            dut.write("stop")
            dut.expect_exact("STOPPED\r\n", timeout=40)
    finally:
        dut.write("stop")
        dut.expect_exact("STOPPED\r\n", timeout=40)


@pytest.mark.parametrize("controller", ["hs", "fs"])
def test_p4_borrowed(dut, controller):
    try:
        for _ in range(3):
            dut.write(f"start {controller} borrowed")
            dut.expect_exact(f"START ok=1 device={controller} speed=1 in=64 out=64 error=0\r\n", timeout=40)
            dut.write("borrowed")
            dut.expect_exact("BORROWED ok=1 rx=8 tx=8 bytes=70469 guards=1 actual=65537 completed=1\r\n", timeout=120)
            dut.write("stop")
            dut.expect_exact("STOPPED\r\n", timeout=40)
    finally:
        dut.write("stop")
        dut.expect_exact("STOPPED\r\n", timeout=40)
