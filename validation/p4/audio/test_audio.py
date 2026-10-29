"""Real isochronous PCM and explicit feedback, without a codec or I2S wiring."""
import pytest


@pytest.mark.parametrize("controller", ["hs", "fs"])
@pytest.mark.parametrize("protocol", [1, 2])
def test_p4_audio(dut, controller, protocol):
    try:
        for _ in range(2):
            dut.write(f"start {controller} {protocol}")
            result = dut.expect(r"START ok=(\d) controller=(hs|fs) protocol=(\d) speed=(\d+) error=(\d+)\r\n", timeout=40)
            assert result.groups() == (b"1", controller.encode(), str(protocol).encode(), b"1", b"0")
            dut.write("exercise")
            result = dut.expect(r"AUDIO ok=(\d) bytes=(\d+) requests=(\d+) bad=(\d+) format=(\d) overruns=(\d+) feedback=(\d+) rejects=(\d+)\r\n", timeout=30)
            assert result.group(1) == b"1"
            assert int(result.group(2)) >= 4096 and int(result.group(3)) > 0
            assert result.group(4) == result.group(6) == result.group(8) == b"0"
            assert result.group(5) == b"1"
            assert (int(result.group(7)) > 0) if protocol == 2 else (int(result.group(7)) == 0)
            dut.write("stop")
            dut.expect_exact("STOPPED\r\n", timeout=40)
    finally:
        dut.write("stop")
        dut.expect_exact("STOPPED\r\n", timeout=40)
