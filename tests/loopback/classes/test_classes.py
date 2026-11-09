"""RAM media only: independent MSC BOT and two-cable MIDI on both P4 ports."""
import pytest


@pytest.mark.parametrize("controller", ["hs", "fs"])
@pytest.mark.parametrize("sector,staging", [(511, 64), (512, 512)])
def test_p4_msc_midi(dut, controller, sector, staging):
    try:
        for _ in range(2):
            dut.write(f"start {controller} {sector} {staging}")
            if controller == "hs" and staging == 64:
                dut.expect_exact("CAPACITY_REJECT ok=1 requested=64 retry=512\r\n", timeout=10)
            result = dut.expect(r"START ok=(\d) controller=(hs|fs) speed=(\d+) block=(\d+) staging=(\d+)\r\n", timeout=40)
            assert result.groups() == (b"1", controller.encode(), b"1", str(sector).encode(), str(max(staging, 512) if controller == "hs" else staging).encode())
            dut.write("exercise")
            result = dut.expect(r"CLASSES ok=(\d) writes=(\d+) reads=(\d+) midi=(\d+) bad=(\d+) guards=(\d)\r\n", timeout=40)
            assert result.groups() == (b"1", b"3", b"3", b"32", b"0", b"1")
            dut.write("stop")
            dut.expect_exact("STOPPED\r\n", timeout=40)
    finally:
        dut.write("stop")
        dut.expect_exact("STOPPED\r\n", timeout=40)
