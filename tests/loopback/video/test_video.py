"""UVC framing and ownership over real USB; synthetic bytes, no image codec."""
import pytest


@pytest.mark.parametrize("controller", ["hs", "fs"])
@pytest.mark.parametrize("bulk", [0, 1])
def test_p4_video(dut, controller, bulk):
    try:
        for _ in range(2):
            dut.write(f"start {controller} {bulk}")
            result = dut.expect(r"START ok=(\d) controller=(hs|fs) bulk=(\d) speed=(\d+) payload=(\d+) error=(\d+)\r\n", timeout=40)
            assert result.groups() == (b"1", controller.encode(), str(bulk).encode(), b"1", b"512", b"0")
            dut.write("exercise")
            result = dut.expect(r"VIDEO ok=(\d) frames=(\d+) results=(\d+) bytes=(\d+) bad=(\d+) guards=(\d)\r\n", timeout=45)
            assert result.groups() == (b"1", b"7", b"7", b"5314", b"0", b"1")
            dut.write("stop")
            dut.expect_exact("STOPPED\r\n", timeout=40)
    finally:
        dut.write("stop")
        dut.expect_exact("STOPPED\r\n", timeout=40)
