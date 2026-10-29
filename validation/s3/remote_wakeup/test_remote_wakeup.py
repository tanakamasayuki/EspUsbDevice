"""Host-observed Device wake request, permission, clear and reset boundaries."""


def test_remote_wakeup(dut, peers):
    device = peers["device"]

    def command(board, text, reply, timeout=15):
        board.write(text)
        board.expect_exact(reply + "\r\n", timeout=timeout)

    def exchange():
        command(device, "arm", "ARMED 1")
        command(dut, "exchange", "EXCHANGED 1")
        command(device, "result", "RESULT 1")

    try:
        command(device, "metadata", "METADATA 1", 30)
        for support in (0, 1):
            command(device, f"start {support}", "STARTED 1", 30)
            command(dut, "start", f"HOST_READY ok=1 capable={support}", 30)
            command(dut, "status", "STATUS valid=1 enabled=0")
            command(device, "state 0", "STATE ready=1 suspended=0 enabled=0")
            command(device, "request", "REQUEST error=259")
            if not support:
                command(dut, "feature 1", "FEATURE value=1 accepted=0 status=0 valid=1")
            for cycle in range(3):
                command(dut, "suspend", "SUSPENDED 1")
                command(device, "state 1", "STATE ready=1 suspended=1 enabled=0")
                command(device, "request", "REQUEST error=259")
                command(dut, "quiet", "WAKE seen=0 resumed=1")
                command(device, "state 0", "STATE ready=1 suspended=0 enabled=0")
                exchange()
                if support:
                    command(dut, "feature 1", "FEATURE value=1 accepted=1 status=1 valid=1")
                    command(device, "request", "REQUEST error=259")  # Active bus.
                    command(dut, "suspend", "SUSPENDED 1")
                    command(device, "state 1", "STATE ready=1 suspended=1 enabled=1")
                    dut.write("wake")  # Poll first, then drive Host resume only after WKUP.
                    dut.expect_exact("WATCHING\r\n", timeout=5)
                    command(device, "request", "REQUEST error=0")
                    dut.expect_exact("WAKE seen=1 resumed=1\r\n", timeout=15)
                    command(device, "state 0", "STATE ready=1 suspended=0 enabled=1")
                    exchange()
                    command(dut, "feature 0", "FEATURE value=0 accepted=1 status=0 valid=1")
            if support:
                command(dut, "feature 1", "FEATURE value=1 accepted=1 status=1 valid=1")
                command(dut, "stop", "STOPPED", 30)
                command(dut, "start", "HOST_READY ok=1 capable=1", 30)
                command(dut, "status", "STATUS valid=1 enabled=0")
                command(device, "state 0", "STATE ready=1 suspended=0 enabled=0")
                exchange()
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
