"""Independent EP0, strict standard requests and runtime event storage."""


def test_control_core(dut, peers):
    device = peers["device"]

    def command(board, text, reply, timeout=10):
        board.write(text)
        board.expect_exact(reply + "\r\n", timeout=timeout)

    def stats(success, reset):
        command(device, "stats", f"STATS input=11 output=7 empty=2 success={success} reset={reset} stop=0 bad=0")

    try:
        device.write("metadata")
        r = device.expect(r"METADATA ok=1 before=(\d+) after=(\d+)\r\n", timeout=30)
        assert r.group(1) == r.group(2)
        for depth in (1, 4, 16, 31):
            command(device, f"start {depth}", "STARTED 1", 30)
            command(dut, "start", "HOST_READY 1", 30)
            command(dut, "data", "DATA 1", 30)
            stats(0, 0)
            command(dut, "errors", "ERRORS 1", 30)
            command(device, "queue", "QUEUED 1")
            command(dut, "deconfigure", "DECONFIGURED 1")
            stats(0, 1)
            command(dut, "configure", "CONFIGURED 1")
            command(device, "queue", "QUEUED 1")
            command(dut, "configure", "CONFIGURED 1")
            stats(0, 2)
            command(device, "queue", "QUEUED 1")
            command(dut, "read", "READ 1")
            stats(1, 2)
            command(device, "queue", "QUEUED 1")
            command(device, "overflow", "OVERFLOW 1")
            stats(1, 3)
            command(dut, "recover", "RECOVERED 1", 30)
            command(device, "queue", "QUEUED 1")
            command(dut, "read", "READ 1")
            stats(2, 3)
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
