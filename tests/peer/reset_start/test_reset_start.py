"""Actual USBRST IRQ must invalidate old work before ENUMDNE/worker reset."""


def test_reset_start(dut, peers):
    device = peers["device"]

    def command(board, text, reply, timeout=10):
        board.write(text)
        board.expect_exact(reply + "\r\n", timeout=timeout)

    try:
        for depth in (4, 16, 31):
            command(device, f"start {depth}", "STARTED 1", 30)
            command(dut, "start", "HOST_READY 1", 30)
            command(device, "prime", "PRIMED 1")
            command(device, "park", "PARKED 1")
            command(dut, "stop", "STOPPED", 30)
            dut.write("start")
            command(device, "observe", "RESET_BEGIN seen=1 before=1 mounted=0 jobs=0 parked=1")
            device.expect_exact("RESET_BLOCKED open=1 data=1 control=1\r\n", timeout=10)
            dut.expect_exact("HOST_READY 1\r\n", timeout=30)
            command(device, "fresh", "FRESH 1 old=2 bad=0")
            command(dut, "exchange", "EXCHANGED 1")
            command(device, "result", "RESULT old=2 success=2 bad=0")
            for board in (dut, device):
                command(board, "stop", "STOPPED", 30)
    finally:
        errors = []
        try:
            command(device, "release", "RELEASED", 10)
        except Exception as exc:
            errors.append(f"release: {exc}")
        for name, board in (("host", dut), ("device", device)):
            try:
                command(board, "stop", "STOPPED", 30)
            except Exception as exc:
                errors.append(f"{name}: {exc}")
        assert not errors, "USB stop failed: " + "; ".join(errors)
