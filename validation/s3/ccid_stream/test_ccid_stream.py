"""Runtime CCID storage, two readers and optional fixed-card notifications."""


def test_ccid_stream(dut, peers):
    device = peers["device"]

    def command(board, text, reply, timeout=10):
        board.write(text)
        board.expect_exact(reply + "\r\n", timeout=timeout)

    try:
        device.write("metadata")
        result = device.expect(r"METADATA ok=1 before=(\d+) after=(\d+)\r\n", timeout=30)
        assert result.group(1) == result.group(2)
        for capacity, removable in ((64, 1), (271, 0), (320, 1), (1024, 0)):
            command(device, f"start {capacity} {removable}", "STARTED 1", 30)
            command(dut, "start", "HOST_READY 1", 30)
            opened = set()
            for index in (0, 1, 0):
                limit = capacity + index * 64
                command(dut, f"open {index} {limit} {removable}", "OPENED 1")
                if removable and index not in opened:
                    command(dut, "notice 1", "NOTICE 1")
                opened.add(index)
                command(dut, "abandon", "ABANDONED 1")
                if removable:
                    command(dut, "notice 1", "NOTICE 1")
                command(dut, "power", "POWERED 1")
                command(dut, "abandon_abort", "ABANDONED 1")
                if removable:
                    command(dut, "notice 1", "NOTICE 1")
                command(dut, "power", "POWERED 1")
                command(dut, "parameters", "PARAMETERS 1")
                for length in (1, min(54, limit - 10), min(118, limit - 10), min(261, limit - 10)):
                    command(dut, f"exchange {length} 0", "EXCHANGED 1")
                command(dut, f"exchange {limit - 10} 1", "EXCHANGED 1")
                command(dut, "errors", "ERRORS 1")
                command(dut, "abort", "ABORTED 1")
                command(dut, "off", "POWER_OFF 1")
                command(dut, "present 1", "PRESENT 1")
                if removable:
                    command(device, f"remove {index}", "REMOVED 1")
                    command(dut, "notice 0", "NOTICE 1")
                    command(dut, "present 0", "PRESENT 1")
                    command(device, f"insert {index}", "INSERTED 1")
                    command(dut, "notice 1", "NOTICE 1")
                    command(dut, "power", "POWERED 1")
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
