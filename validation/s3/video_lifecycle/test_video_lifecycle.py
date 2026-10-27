"""Video format validation and stopping DMA before releasing a borrowed frame."""


def test_video_lifecycle(dut, peers):
    device = peers["device"]
    try:
        device.write("metadata")
        r = device.expect(r"METADATA ok=1 before=(\d+) after=(\d+)\r\n", timeout=30)
        assert r.group(1) == r.group(2)
        device.write("start")
        device.expect_exact("STARTED 1\r\n", timeout=30)
        dut.write("start")
        dut.expect_exact("HOST_READY 1\r\n", timeout=30)
        device.write("intruder")
        device.expect_exact("INTRUDER 1\r\n", timeout=5)
        for count, length in enumerate((257, 511, 512, 1021), 1):
            device.write(f"frame {length}")
            device.expect_exact("QUEUED 1\r\n", timeout=5)
            device.expect_exact(f"COMPLETE {count}\r\n", timeout=10)
            dut.write(f"check {count} {length}")
            dut.expect_exact(f"FRAME 1 count={count} length={length} bad=0\r\n", timeout=10)
        device.write("drop")
        device.expect_exact("DROPPED 1\r\n", timeout=30)
    finally:
        errors = []
        for name, board in (("host", dut), ("device", device)):
            try:
                board.write("stop")
                board.expect_exact("STOPPED\r\n", timeout=30)
            except Exception as exc:
                errors.append(f"{name}: {exc}")
        assert not errors, "USB stop failed: " + "; ".join(errors)
