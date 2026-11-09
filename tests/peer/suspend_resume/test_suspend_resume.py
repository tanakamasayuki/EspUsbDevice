"""Real S3 bus suspend/resume without resetting the configuration or loans."""


def test_suspend_resume(dut, peers):
    device = peers["device"]

    def command(board, text, reply, timeout=15):
        board.write(text)
        board.expect_exact(reply + "\r\n", timeout=timeout)

    def state(suspended):
        command(device, f"state {suspended}",
                f"STATE started=1 ready=1 suspended={suspended}")

    def result(count, pending=0, quiet=False):
        command(device, f"{'quiet' if quiet else 'result'} {count}",
                f"RESULT count={count} expected={count} txPending={pending} rxPending={pending} bad=0")

    try:
        command(device, "metadata", "METADATA 1")
        for depth in (1, 16, 31):
            command(device, f"start {depth}", "STARTED 1")
            command(dut, "start", "HOST_READY 1", 30)
            state(0)
            # Already armed loans survive bus idle; no Host transfer is queued.
            command(device, "queue", "QUEUED 1")
            command(dut, "suspend", "SUSPENDED 1")
            state(1)
            result(0, pending=1, quiet=True)
            command(dut, "resume", "RESUMED 1")
            state(0)
            command(dut, "exchange", "EXCHANGED 1")
            result(2)
            # New loans during suspend also wait for real bus activity.
            command(dut, "suspend", "SUSPENDED 1")
            state(1)
            command(device, "queue", "QUEUED 1")
            result(2, pending=1, quiet=True)
            command(dut, "resume", "RESUMED 1")
            state(0)
            command(dut, "exchange", "EXCHANGED 1")
            result(4)
            # Cancellation must return buffers without waking or resetting USB.
            command(dut, "suspend", "SUSPENDED 1")
            state(1)
            command(device, "queue", "QUEUED 1")
            command(device, "cancel", "CANCELLED 1")
            result(6)
            state(1)
            command(dut, "resume", "RESUMED 1")
            state(0)
            command(device, "queue", "QUEUED 1")
            command(dut, "exchange", "EXCHANGED 1")
            result(8)
            # end() must stop DMA and return loans even with no bus clocks.
            command(dut, "suspend", "SUSPENDED 1")
            state(1)
            command(device, "queue", "QUEUED 1")
            result(8, pending=1, quiet=True)
            command(device, "stop", "STOPPED")
            result(10)
            command(dut, "stop", "STOPPED", 30)
            command(device, "metadata", "METADATA 1")
    finally:
        errors = []
        for name, board in (("host", dut), ("device", device)):
            try:
                command(board, "stop", "STOPPED", 30)
            except Exception as exc:
                errors.append(f"{name}: {exc}")
        assert not errors, "USB stop failed: " + "; ".join(errors)
