"""Runtime MSC staging sizes, optional write path, media/errors and recovery."""


def test_msc_storage(dut, peers):
    device = peers["device"]
    try:
        device.write("metadata")
        result = device.expect(r"METADATA ok=1 before=(\d+) after=(\d+)\r\n", timeout=30)
        assert result.group(1) == result.group(2), "MSC configuration leaked heap"
        for capacity, mode in ((64, 0), (128, 0), (512, 0), (4096, 0), (192, 1), (64, 2), (128, 3)):
            device.write(f"start {capacity} {mode}")
            device.expect_exact(f"STARTED 1 capacity={capacity}\r\n", timeout=30)
            dut.write("start")
            dut.expect_exact("HOST_READY 1\r\n", timeout=30)
            if mode != 2:
                dut.write("roundtrip")
                dut.expect_exact("ROUNDTRIP 1\r\n", timeout=30)
            device.write("absent")
            device.expect_exact("MEDIA_CHANGED\r\n", timeout=5)
            dut.write("error 1")
            dut.expect_exact("ERROR_SENSE 1 key=2 asc=58\r\n", timeout=15)
            device.write("present")
            device.expect_exact("MEDIA_CHANGED\r\n", timeout=5)
            dut.write("ready")
            dut.expect_exact("READY 1\r\n", timeout=5)
            device.write("stats")
            device.expect(r"STATS reads=\d+ writes=\d+ offsets=1 guards=1\r\n", timeout=5)
            # Host 2.10.1 resets immediately on a data-stage stall; its public
            # recovery leaves a toggle mismatch. Test rejection last here.
            # The manual BOT probe verifies CSW/sense and recovery independently.
            if mode == 2:
                dut.write("error 0")
            else:
                device.write("fail")
                device.expect_exact("IO_CHANGED\r\n", timeout=5)
                dut.write("error 2")
            dut.expect_exact("ERROR_REJECTED 1\r\n", timeout=15)
            for board in (dut, device):
                board.write("stop")
                board.expect_exact("STOPPED\r\n", timeout=30)
    finally:
        errors = []
        for name, board in (("host", dut), ("device", device)):
            try:
                board.write("stop")
                board.expect_exact("STOPPED\r\n", timeout=30)
            except Exception as exc:
                errors.append(f"{name}: {exc}")
        assert not errors, "USB stop failed: " + "; ".join(errors)
