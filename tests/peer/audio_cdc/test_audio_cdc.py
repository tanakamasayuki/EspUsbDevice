"""Periodic duplex Audio and CDC bulk share the S3 controller and USB worker."""


def test_audio_cdc(dut, peers):
    device = peers["device"]

    def command(board, text, reply, timeout=15):
        board.write(text)
        board.expect_exact(reply + "\r\n", timeout=timeout)

    try:
        for protocol, order, rate in ((2, 0, 24000), (2, 1, 48000), (1, 0, 48000), (1, 1, 24000)):
            command(device, f"start {protocol} {order} {rate}", "STARTED 1 error=0", 30)
            command(dut, "start", "HOST_READY 1", 30)
            dut.write(f"run {rate}")
            result = dut.expect(r"CONCURRENT ok=[01] pcm=(\d+) filled=(\d+) bad=0 echoes=(\d+) bytes=(\d+) overlap=(\d+) feedback=(\d+) rejects=0\r\n", timeout=20)
            succeeded = b"ok=1" in result.group(0)
            # Read device state before asserting so a failed concurrent exchange is diagnosable.
            device.write("stats")
            stats = device.expect(r"COMPOSITE rx=(\d+) tx=(\d+) consumed=(\d+) bad=0 cdcRx=(\d+) cdcTx=(\d+) baud=(\d+)\r\n", timeout=5)
            assert succeeded, result.group(0)
            assert stats.group(6) == b"230400"
            assert int(result.group(1)) >= rate * 2 and int(result.group(2)) > 0
            assert int(result.group(3)) >= 10 and int(result.group(5)) > 0
            if protocol == 2:
                assert int(result.group(6)) > 0
            assert all(int(stats.group(i)) >= rate * 2 for i in (1, 2, 3))
            assert stats.group(4) == stats.group(5) == result.group(4)
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
