"""Recover multi-packet HID reports without changing the other endpoint PID."""


def test_hid_standard(dut, peers):
    device = peers["device"]

    def command(board, text, reply, timeout=10):
        board.write(text)
        board.expect_exact(reply + "\r\n", timeout=timeout)

    count = 0

    def received():
        nonlocal count
        count += 1
        command(device, f"stats {count}", f"STATS outputs={count} bad=0")

    def fresh():
        command(device, "send", "SENT 1")
        command(dut, "read 0 129", "TRANSFER 1")
        command(dut, "write 0 129", "TRANSFER 1")
        received()

    try:
        command(device, "start", "STARTED 1", 30)
        command(dut, "start", "HOST_READY 1", 30)
        for _ in range(3):
            command(device, "send", "SENT 1")
            command(dut, "read 0 64", "TRANSFER 1")
            command(dut, "write 0 64", "TRANSFER 1")
            command(device, "send", "SENT 0")
            command(dut, "alternate", "ALTERNATE 1")
            command(device, f"stats {count}", f"STATS outputs={count} bad=0")
            fresh()
        for reading in (1, 0):
            for halt in (1, 0):
                command(dut, "alternate", "ALTERNATE 1")
                command(device, "send", "SENT 1")
                # Advance only the unaffected direction to DATA1.
                command(dut, "write 0 64" if reading else "read 0 64", "TRANSFER 1")
                command(dut, f"feature {reading} {halt}", "FEATURE 1")
                if reading:
                    command(device, "send", "SENT 0" if halt else "SENT 1")
                if halt:
                    command(dut, f"feature {reading} 0", "FEATURE 1")
                    if reading:
                        command(device, "send", "SENT 1")
                command(dut, "read 0 129" if reading else "read 64 65", "TRANSFER 1")
                command(dut, "write 64 65" if reading else "write 0 129", "TRANSFER 1")
                received()
        # Both endpoints have sent one packet before HALT. Recreate host pipes
        # after CLEAR because this SDK resumes their previous PID on clear.
        command(dut, "alternate", "ALTERNATE 1")
        command(device, "send", "SENT 1")
        command(dut, "read 0 64", "TRANSFER 1")
        command(dut, "write 0 64", "TRANSFER 1")
        for reading in (1, 0):
            command(dut, f"feature {reading} 1", "FEATURE 1")
            command(dut, f"feature {reading} 0", "FEATURE 1")
        command(dut, "reclaim", "RECLAIMED 1")
        command(device, f"stats {count}", f"STATS outputs={count} bad=0")
        fresh()
    finally:
        errors = []
        for name, board in (("host", dut), ("device", device)):
            try:
                command(board, "stop", "STOPPED", 30)
            except Exception as exc:
                errors.append(f"{name}: {exc}")
        assert not errors, "USB stop failed: " + "; ".join(errors)
