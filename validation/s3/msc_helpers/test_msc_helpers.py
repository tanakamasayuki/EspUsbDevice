"""RAM block and FAT12 helpers through actual BOT, without external storage."""


def test_msc_helpers(dut, peers, request):
    device = peers["device"]
    # Staging must be a multiple of the bulk MPS: scale FS sizes on HS.
    scale = 8 if "_hs_" in request.node.name else 1

    def command(board, text, reply, timeout=15):
        board.write(text)
        board.expect_exact(reply + "\r\n", timeout=timeout)

    try:
        for cycle in range(2):
            for mode, capacity in ((0, 64), (0, 192), (1, 64), (1, 512)):
                capacity = min(capacity * scale, 4096)
                command(device, f"start {mode} {capacity}", f"STARTED ok=1 mode={mode} capacity={capacity}", 30)
                command(dut, f"start {mode}", "HOST_READY 1", 30)
                command(dut, "fat" if mode else "raw", "FAT_ROUNDTRIP 1" if mode else "RAW_ROUNDTRIP 1", 30)
                command(device, "verify", "VERIFIED ok=1 guards=1")
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
