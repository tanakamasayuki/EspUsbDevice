"""Old queued completions must not release loans in a reselected configuration."""


def test_configuration_epoch(dut, peers):
    device = peers["device"]

    def command(board, text, reply, timeout=10):
        board.write(text)
        board.expect_exact(reply + "\r\n", timeout=timeout)

    try:
        for depth in (4, 16, 31):
            command(device, f"start {depth}", "STARTED 1", 30)
            command(dut, "start", "HOST_READY 1", 30)
            for _ in range(3):
                command(device, "prime", "PRIMED 1")
                command(device, "park", "PARKED 1")
                command(dut, "configure", "CONFIG_SUBMITTED 1")
                command(device, "inject", "INJECTED 1")
                command(dut, "complete", "CONFIGURED 1")
                command(device, "pending", "STATE old=2 success=0 tx=1 rx=1 bad=0")
                command(dut, "exchange", "EXCHANGED 1")
                command(device, "result", "STATE old=2 success=2 tx=0 rx=0 bad=0")
            for board in (dut, device):
                command(board, "stop", "STOPPED", 30)
    finally:
        errors = []
        # Release any parked worker even when a host request failed.
        try:
            command(device, "release", "RELEASED")
        except Exception as exc:
            errors.append(f"release: {exc}")
        for name, board in (("host", dut), ("device", device)):
            try:
                command(board, "stop", "STOPPED", 30)
            except Exception as exc:
                errors.append(f"{name}: {exc}")
        assert not errors, "USB stop failed: " + "; ".join(errors)
