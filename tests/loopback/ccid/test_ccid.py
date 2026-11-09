"""Simulated card backend: actual CCID wire messages, no physical card reader."""
import pytest


@pytest.mark.parametrize("controller", ["hs", "fs"])
@pytest.mark.parametrize("removable", [0, 1])
def test_p4_ccid(dut, controller, removable):
    try:
        for _ in range(2):
            dut.write(f"start {controller} {removable}")
            result = dut.expect(r"START ok=(\d) controller=(hs|fs) removable=(\d) speed=(\d+) capacity=(\d+) error=(\d+)\r\n", timeout=40)
            assert result.groups() == (b"1", controller.encode(), str(removable).encode(), b"1", b"1024", b"0")
            dut.write("exercise")
            result = dut.expect(r"CCID ok=(\d) apdu=(\d+) escape=(\d+) callbacks=(\d+)/(\d+) slots=(\d+)\r\n", timeout=40)
            assert result.groups() == (b"1", b"9", b"3", b"9", b"3", b"2" if removable else b"1")
            dut.write("stop")
            dut.expect_exact("STOPPED\r\n", timeout=40)
    finally:
        dut.write("stop")
        dut.expect_exact("STOPPED\r\n", timeout=40)
