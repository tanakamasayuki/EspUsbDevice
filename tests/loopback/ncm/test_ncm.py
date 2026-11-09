"""Raw Ethernet over real NCM USB transfers; no same-MCU IP routing."""
import pytest


@pytest.mark.parametrize("controller", ["hs", "fs"])
@pytest.mark.parametrize("capacity", [2048, 4096])
def test_p4_ncm(dut, controller, capacity):
    try:
        for _ in range(2):
            dut.write(f"start {controller} {capacity}")
            result = dut.expect(r"START ok=(\d) controller=(hs|fs) capacity=(\d+) speed=(\d+) in=(\d+) out=(\d+) error=(\d+)\r\n", timeout=40)
            cap = str(capacity).encode()
            assert result.groups() == (b"1", controller.encode(), cap, b"1", cap, cap, b"0")
            dut.write("exercise")
            result = dut.expect(r"NCM ok=(\d) frames=(\d+) echoes=(\d+) bytes=(\d+) rx=(\d+) tx=(\d+) invalid=(\d+) bad=(\d+)\r\n", timeout=45)
            assert result.groups() == (b"1", b"9", b"9", b"3563", b"9", b"9", b"0", b"0")
            dut.write("stop")
            dut.expect_exact("STOPPED\r\n", timeout=40)
    finally:
        dut.write("stop")
        dut.expect_exact("STOPPED\r\n", timeout=40)
