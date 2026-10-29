"""Typed HID serializers, release/click and GET_REPORT, alone and merged."""


def test_hid_typed(dut, peers):
    device = peers["device"]

    def command(board, text, reply, timeout=15):
        board.write(text)
        board.expect_exact(reply + "\r\n", timeout=timeout)

    try:
        for cycle in range(2):
            for mode in range(4):
                command(device, f"start {mode}", "STARTED 1", 30)
                command(dut, "start", "HOST_READY 1", 30)
                if mode in (2, 3):
                    command(dut, "system_descriptor", "SYSTEM_DESCRIPTOR ok=1 power=129 sleep=130 wake=131 null=1")
                for kind in (range(3) if mode == 3 else (mode,)):
                    for phase in range(3 if kind == 0 else 4):
                        command(dut, f"arm {kind} {phase}", "ARMED")
                        command(device, f"send {kind} {phase}", f"SENT kind={kind} phase={phase} ok=1")
                        count = 2 if phase == 3 else 1
                        length = (12, 3, 2)[kind]
                        command(dut, "check", f"REPORT kind={kind} phase={phase} ok=1 count={count} bytes={length}")
                        if kind == 2 and phase == 0:
                            command(dut, "silence_start", "WATCHING")
                            command(device, "invalid_system", "INVALID_SYSTEM ok=1 rejected=7 usage=129")
                            command(dut, "silence_check", "SILENCE ok=1 unexpected=0")
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
