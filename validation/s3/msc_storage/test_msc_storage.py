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
            dut.write("changed")
            dut.expect_exact("CHANGED_SENSE 1 key=6 asc=40\r\n", timeout=5)
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


def test_msc_cdc_concurrent(dut, peers):
    """Small CDC rings keep progressing during BOT and media-not-present errors."""
    device = peers["device"]

    def command(board, text, reply, timeout=15):
        board.write(text)
        board.expect_exact(reply + "\r\n", timeout=timeout)

    try:
        for capacity in (64, 4096):
            command(device, f"start {capacity} 3", f"STARTED 1 capacity={capacity}", 30)
            command(dut, "start", "HOST_READY 1", 30)
            command(dut, "cdc_start", "CDC_STARTED 1")
            command(dut, "roundtrip", "ROUNDTRIP 1", 30)
            dut.write("cdc_progress")
            dut.expect(r"CDC_PROGRESS ok=1 echoes=\d+ overlap=\d+\r\n", timeout=15)
            command(device, "absent", "MEDIA_CHANGED")
            command(dut, "error 1", "ERROR_SENSE 1 key=2 asc=58")
            dut.write("cdc_progress")
            dut.expect(r"CDC_PROGRESS ok=1 echoes=\d+ overlap=\d+\r\n", timeout=15)
            command(device, "present", "MEDIA_CHANGED")
            command(dut, "changed", "CHANGED_SENSE 1 key=6 asc=40")
            command(dut, "ready", "READY 1")
            command(dut, "roundtrip", "ROUNDTRIP 1", 30)
            dut.write("cdc_stop")
            result = dut.expect(r"CDC_RESULT stopped=1 echoes=(\d+) bytes=(\d+) overlap=(\d+) bad=0\r\n", timeout=15)
            assert int(result.group(1)) >= 10
            assert int(result.group(3)) > 0, "CDC completed no echoes while an MSC call was pending"
            device.write("cdc_stats")
            counts = device.expect(r"CDC_BYTES received=(\d+) sent=(\d+)\r\n", timeout=5)
            assert counts.group(1) == counts.group(2) == result.group(2)
            device.write("stats")
            device.expect(r"STATS reads=\d+ writes=\d+ offsets=1 guards=1\r\n", timeout=5)
            for board in (dut, device):
                command(board, "stop", "STOPPED", 30)
    finally:
        errors = []
        for name, board in (("host", dut), ("device", device)):
            try:
                command(board, "stop", "STOPPED", 30)
            except Exception as exc:
                errors.append(f"{name}: {exc}")
        assert not errors, "USB stop failed: " + "; ".join(errors)
