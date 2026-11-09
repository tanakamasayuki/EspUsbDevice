"""Sparse endpoint numbers use the controller limit and compact TX FIFO map."""


def test_endpoint_number(dut, peers, request):
    device = peers["device"]
    mps = 512 if "_hs_" in request.node.name else 64  # bulk MPS of the link
    last = 15 if mps == 512 else 6  # highest endpoint number of the controller

    def command(board, text, reply, timeout=15):
        board.write(text)
        board.expect_exact(reply + "\r\n", timeout=timeout)

    try:
        for endpoint in (5, last, 1, last):
            command(device, f"start {endpoint}", f"STARTED ep={endpoint} ok=1 error=0 irq=1", 30)
            command(dut, "start", f"HOST_READY mps_in={mps} mps_out={mps}", 30)
            for length in (1, mps - 1, mps, mps + 1, 2 * mps + 1):
                command(dut, f"exchange {length}", f"ROUNDTRIP_OK {length}")
            command(device, "stats", f"ENDPOINT ep={endpoint} fifo=1 guards=1")
            for board in (dut, device):
                command(board, "stop", "STOPPED", 30)
        command(device, f"start {last + 1}", f"STARTED ep={last + 1} ok=0 error=260 irq=0", 30)
    finally:
        errors = []
        for name, board in (("host", dut), ("device", device)):
            try:
                command(board, "stop", "STOPPED", 30)
            except Exception as exc:
                errors.append(f"{name}: {exc}")
        assert not errors, "USB stop failed: " + "; ".join(errors)
