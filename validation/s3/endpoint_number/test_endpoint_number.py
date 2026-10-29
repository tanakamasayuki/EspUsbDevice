"""Sparse endpoint numbers use the controller limit and compact TX FIFO map."""


def test_endpoint_number(dut, peers):
    device = peers["device"]

    def command(board, text, reply, timeout=15):
        board.write(text)
        board.expect_exact(reply + "\r\n", timeout=timeout)

    try:
        for endpoint in (5, 6, 1, 6):
            command(device, f"start {endpoint}", f"STARTED ep={endpoint} ok=1 error=0 irq=1", 30)
            command(dut, "start", "HOST_READY mps_in=64 mps_out=64", 30)
            for length in (1, 63, 64, 65, 129):
                command(dut, f"exchange {length}", f"ROUNDTRIP_OK {length}")
            command(device, "stats", f"ENDPOINT ep={endpoint} fifo=1 guards=1")
            for board in (dut, device):
                command(board, "stop", "STOPPED", 30)
        command(device, "start 7", "STARTED ep=7 ok=0 error=260 irq=0", 30)
    finally:
        errors = []
        for name, board in (("host", dut), ("device", device)):
            try:
                command(board, "stop", "STOPPED", 30)
            except Exception as exc:
                errors.append(f"{name}: {exc}")
        assert not errors, "USB stop failed: " + "; ".join(errors)
