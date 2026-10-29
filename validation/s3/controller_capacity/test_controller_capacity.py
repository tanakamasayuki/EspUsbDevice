"""Reject physical IN oversubscription before IRQ/worker startup; retain settings."""


def test_controller_capacity(dut, peers):
    device = peers["device"]

    def command(board, text, reply, timeout=15):
        board.write(text)
        board.expect_exact(reply + "\r\n", timeout=timeout)

    try:
        for mode in range(3):
            device.write(f"attempt {mode}")
            r = device.expect(fr"LIMITS mode={mode} rejected=3 error=260 irq=0 released=1 before=(\d+) after=(\d+) metaRejected=1\r\n", timeout=30)
            assert r.group(1) == r.group(2), "Oversubscribed configuration leaked heap"
            command(device, "recover", "RECOVERED 1", 30)
            command(dut, "start", "HOST_READY 1", 30)
            for port in (0, 1):
                for n in (1, 63, 64, 65, 129):
                    command(dut, f"exchange {port} {n}", f"EXCHANGE port={port} bytes={n} ok=1")
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
