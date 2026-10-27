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


def test_video_halt(dut, peers):
    device = peers["device"]
    counts = [dict(results=0, success=0, cancel=0, error=0, unknown=0) for _ in range(2)]

    def command(board, text, reply, timeout=10):
        board.write(text)
        board.expect_exact(reply + "\r\n", timeout=timeout)

    def stats(i, pending=0):
        values = counts[i]
        command(device, f"halt_stats {i} {values['results']}",
                f"HALT_STATS {i} " + " ".join(f"{k}={v}" for k, v in values.items()) +
                f" bad=0 pending={pending}")

    def queue(i, length):
        command(device, f"queue {i} {length}", f"QUEUED {i} 0")
        command(device, f"armed {i}", "ARMED 1")

    def read(i, length):
        command(dut, f"read {i} {length}", "FRAME 1")
        counts[i]["results"] += 1
        counts[i]["success"] += 1
        stats(i)

    def prepare(i):
        # IDF clear retains the old host PID. Reset only this interface before
        # the scenario; every recovery below uses HALT/CLEAR without reselect.
        command(dut, f"prepare {i}", "PREPARED 1")
        command(device, f"ready {i}", "READY 1")

    try:
        command(device, "start", "STARTED 1", 30)
        command(dut, "start", "HOST_READY 1", 30)
        for i in (0, 1):
            other = 1 - i
            fresh = 512 if i else 257
            for partial in (False, True):
                prepare(i)
                queue(i, 512 if i else 65536)
                if partial:
                    # Both payload limits occupy two FS packets, leaving the
                    # affected host pipe at DATA0 after this partial frame.
                    command(dut, f"packet {i}", "PACKET 1")
                    command(device, f"armed {i}", "ARMED 1")
                queue(other, 512 if other else 257)
                command(dut, f"set {i}", "FEATURE 1")
                command(device, f"halted {i}", "HALTED 1")
                counts[i]["results"] += 1
                counts[i]["error"] += 1
                counts[i]["unknown"] += 1
                stats(i)
                read(other, 512 if other else 257)
                # Repeated HALT must neither duplicate the old completion nor
                # permit a new loan. CLEAR preserves the negotiated commit.
                command(dut, f"set {i}", "FEATURE 1")
                command(device, f"halted {i}", "HALTED 1")
                stats(i)
                command(dut, f"clear {i}", "FEATURE 1")
                command(device, f"ready {i}", "READY 1")
                stats(i)
                queue(i, fresh)
                read(i, fresh)
            prepare(i)
            queue(i, 512 if i else 65536)
            command(dut, f"clear {i}", "FEATURE 1")
            command(device, f"ready {i}", "READY 1")
            counts[i]["results"] += 1
            counts[i]["cancel"] += 1
            stats(i)
            command(dut, f"clear {i}", "FEATURE 1")
            command(device, f"ready {i}", "READY 1")
            stats(i)
            queue(i, fresh)
            read(i, fresh)
    finally:
        errors = []
        for name, board in (("host", dut), ("device", device)):
            try:
                command(board, "stop", "STOPPED", 30)
            except Exception as exc:
                errors.append(f"{name}: {exc}")
        assert not errors, "USB stop failed: " + "; ".join(errors)
