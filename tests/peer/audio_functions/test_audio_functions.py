"""Four UAC1 duplex Functions share endpoint numbers across directions."""


def test_audio_functions(dut, peers):
    device = peers["device"]

    def command(board, text, reply, timeout=15):
        board.write(text)
        board.expect_exact(reply + "\r\n", timeout=timeout)

    try:
        for cycle in range(2):
            command(device, "start", "STARTED ok=1 error=0 span=1", 30)
            command(dut, "start", "HOST_READY 1", 30)
            visited = set()
            for index in (0, 3, 1, 2, 0):
                dut.write(f"run {index}")
                r = dut.expect(fr"FUNCTION index={index} ok=1 rx=(\d+) tx=(\d+) filled=(\d+) bad=0 lost=\d+\r\n", timeout=15)
                assert int(r.group(1)) >= 4096 and int(r.group(2)) >= 4096 and int(r.group(3)) > 0
                visited.add(index)
                for other in range(4):
                    device.write(f"stats {other}")
                    stats = device.expect(fr"FUNCTION index={other} rx=(\d+) tx=(\d+) consumed=(\d+) bad=0\r\n", timeout=5)
                    if other in visited:
                        assert all(int(stats.group(i)) >= 4096 for i in (1, 2, 3))
                    else:
                        assert all(int(stats.group(i)) == 0 for i in (1, 2, 3))
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
