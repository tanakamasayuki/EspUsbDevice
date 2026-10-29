"""Sustained framed traffic across repeated physical Host re-enumeration."""


def test_vendor_soak(dut, peers):
    device = peers["device"]
    try:
        device.write("start")
        device.expect_exact("STARTED 1\r\n", timeout=30)
        dut.write("run 1000")
        host = dut.expect(r"SOAK ok=([01]) frames=(\d+) bytes=(\d+) reconnects=(\d+)\r\n", timeout=240)
        device.write("finish")
        r = device.expect(r"FINISHED ok=([01]) rx=(\d+) tx=(\d+) bad=(\d+) resets=(\d+) guards=([01]) before=(\d+) after=(\d+)\r\n", timeout=30)
        assert host.groups() == (b"1", b"1000", b"713000", b"39")
        assert r.group(1) == b"1" and r.group(2) == r.group(3) == b"713000" and r.group(4) == b"0" and r.group(6) == b"1"
        assert int(r.group(5)) >= 40
        assert r.group(7) == r.group(8), "Repeated bus re-enumeration leaked Device heap"
    finally:
        errors = []
        for name, board in (("host", dut), ("device", device)):
            try:
                board.write("stop")
                board.expect_exact("STOPPED\r\n", timeout=30)
            except Exception as exc:
                errors.append(f"{name}: {exc}")
        assert not errors, "USB stop failed: " + "; ".join(errors)
