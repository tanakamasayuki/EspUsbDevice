"""One P4, two native controllers, a physical FS link in both role mappings."""
import pytest


@pytest.mark.parametrize("controller", ["hs", "fs"])
def test_p4_loopback(dut, controller):
    try:
        for _ in range(3):
            dut.write(f"start {controller}")
            dut.expect_exact(f"START ok=1 device={controller} speed=1 in=64 out=64 error=0\r\n", timeout=40)
            dut.write("exercise")
            result = dut.expect(r"EXERCISE ok=(\d) copies=(\d+) bytes=(\d+) direct=(\d+) echoed=(\d+) bad=(\d+) guards=(\d)\r\n", timeout=30)
            assert result.groups() == (b"1", b"11", b"6210", b"128", b"6210", b"0", b"1")
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


@pytest.mark.parametrize("controller", ["hs", "fs"])
def test_p4_composite(dut, controller):
    try:
        for _ in range(3):
            dut.write(f"start {controller} composite")
            dut.expect_exact(f"START ok=1 device={controller} speed=1 in=64 out=64 error=0\r\n", timeout=40)
            dut.write("exercise")
            result = dut.expect(r"EXERCISE ok=(\d) copies=(\d+) bytes=(\d+) direct=(\d+) echoed=(\d+) bad=(\d+) guards=(\d)\r\n", timeout=30)
            assert result.groups() == (b"1", b"11", b"6210", b"128", b"6210", b"0", b"1")
            dut.write("composite")
            dut.expect_exact("COMPOSITE ok=1 cdc=4 bytes=707 press=3 release=3 baud=230400 bad=0\r\n", timeout=60)
            dut.write("stop")
            dut.expect_exact("STOPPED\r\n", timeout=40)
    finally:
        dut.write("stop")
        dut.expect_exact("STOPPED\r\n", timeout=40)
