"""Per-Function UVC controls, pending ownership and MJPEG/YUY2 wire data."""


def test_video_control(dut, peers):
    device = peers["device"]

    def command(board, text, reply, timeout=10):
        board.write(text)
        board.expect_exact(reply + "\r\n", timeout=timeout)

    def stats(i, result, success, cancel, stopped, pending, commits, payload):
        command(device, f"stats {i}",
                f"STATS {i} results={result} success={success} cancel={cancel} "
                f"stopped={stopped} bad=0 pending={pending} commits={commits} payload={payload}")

    try:
        command(device, "start", "STARTED 1", 30)
        command(dut, "start", "HOST_READY 1", 30)
        stats(0, 0, 0, 0, 0, 0, 1, 65)
        stats(1, 0, 0, 0, 0, 0, 1, 128)
        command(device, "invalid 0", "INVALID 1")
        command(device, "invalid 1", "INVALID 1")
        command(device, "queue 0 65536", "QUEUED 0 0")
        command(device, "queue 1 512", "QUEUED 1 0")
        command(device, "busy", "BUSY 1")
        command(dut, "alternate", "ALTERNATE 1")
        stats(0, 1, 0, 1, 0, 0, 1, 65)
        stats(1, 0, 0, 0, 0, 1, 1, 128)
        command(dut, "read 1 512", "FRAME 1")
        stats(1, 1, 1, 0, 0, 0, 1, 128)
        command(device, "queue 0 257", "QUEUED 0 0")
        command(dut, "read 0 257", "FRAME 1")
        stats(0, 2, 1, 1, 0, 0, 1, 65)
        command(dut, "errors", "ERRORS 1", 30)
        stats(0, 2, 1, 1, 0, 0, 2, 65)
        stats(1, 1, 1, 0, 0, 0, 2, 128)
        command(device, "queue 0 65536", "QUEUED 0 0")
        command(dut, "recommit", "RECOMMIT 1")
        stats(0, 3, 1, 2, 0, 0, 3, 65)
        # 62 data + 2 header = one full FS packet, below payload limit 65.
        command(device, "queue 0 62", "QUEUED 0 0")
        command(dut, "read 0 62", "FRAME 1")
        stats(0, 4, 2, 2, 0, 0, 3, 65)
        command(device, "queue 0 65536", "QUEUED 0 0")
        command(device, "cancel 0", "CANCEL_REQUEST 0")
        stats(0, 5, 2, 3, 0, 0, 3, 65)
        command(device, "queue 1 512", "QUEUED 1 0")
        command(dut, "read 1 512", "FRAME 1")
        stats(1, 2, 2, 0, 0, 0, 2, 128)
        command(device, "queue 0 65536", "QUEUED 0 0")
        command(device, "queue 1 512", "QUEUED 1 0")
        command(device, "stop", "STOPPED", 30)
        stats(0, 6, 2, 3, 1, 0, 3, 0)
        stats(1, 3, 2, 0, 1, 0, 2, 0)
    finally:
        errors = []
        for name, board in (("host", dut), ("device", device)):
            try:
                command(board, "stop", "STOPPED", 30)
            except Exception as exc:
                errors.append(f"{name}: {exc}")
        assert not errors, "USB stop failed: " + "; ".join(errors)
