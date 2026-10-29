"""External RAM is CPU-owned storage/rings; USB DMA staging remains internal."""
import pytest


@pytest.mark.parametrize("controller", ["hs", "fs"])
def test_p4_psram(dut, controller):
    try:
        for _ in range(2):
            dut.write(f"start {controller}")
            memory = dut.expect(r"MEMORY revision=(\d+) cpu=(\d+) psram=(\d+) heap=(\d+)\r\n", timeout=40)
            assert 0 < int(memory.group(1)) < 300
            assert int(memory.group(3)) > 65536 and int(memory.group(4)) > 65536
            result = dut.expect(r"START ok=(\d) controller=(hs|fs) speed=(\d+) external=(\d)\r\n", timeout=30)
            assert result.groups() == (b"1", controller.encode(), b"1", b"1")
            dut.write("exercise")
            result = dut.expect(r"PSRAM ok=(\d) blocks=(\d+) echoes=(\d+) bad=(\d+) guards=(\d)\r\n", timeout=40)
            assert result.groups() == (b"1", b"21", b"40", b"0", b"1")
            dut.write("stop")
            dut.expect_exact("STOPPED\r\n", timeout=40)
    finally:
        dut.write("stop")
        dut.expect_exact("STOPPED\r\n", timeout=40)
