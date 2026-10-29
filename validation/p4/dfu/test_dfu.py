"""DFU EP0 download, manifest, abort and runtime detach using a RAM backend."""
import pytest


@pytest.mark.parametrize("controller", ["hs", "fs"])
def test_p4_dfu(dut, controller):
    try:
        for _ in range(2):
            dut.write(f"start {controller}")
            result = dut.expect(r"START ok=(\d) controller=(hs|fs) speed=(\d+) capacity=(\d+) error=(\d+)\r\n", timeout=40)
            assert result.groups() == (b"1", controller.encode(), b"1", b"1024", b"0")
            dut.write("exercise")
            result = dut.expect(r"DFU ok=(\d) blocks=(\d+) bytes=(\d+) manifests=(\d+) aborted=(\d) bad=(\d+) detach=(\d+)\r\n", timeout=45)
            assert result.groups() == (b"1", b"9", b"1665", b"1", b"1", b"0", b"1")
            dut.write("stop")
            dut.expect_exact("STOPPED\r\n", timeout=40)
    finally:
        dut.write("stop")
        dut.expect_exact("STOPPED\r\n", timeout=40)
