"""Optional event storage, overflow, stopped configuration and USB controls."""


def test_audio_events(dut, peers):
    device = peers["device"]
    try:
        device.write("metadata")
        result = device.expect(r"METADATA ok=1 before=(\d+) after=(\d+)\r\n", timeout=30)
        assert result.group(1) == result.group(2), "Audio event storage leaked heap"
        for capacity in (0, 1, 8, 17):
            device.write(f"start {capacity}")
            device.expect_exact(f"STARTED 1 capacity={capacity}\r\n", timeout=30)
            dut.write("start")
            dut.expect_exact("HOST_READY 1\r\n", timeout=30)
            device.write("intruder")
            device.expect_exact("INTRUDER_REJECTED 1\r\n", timeout=5)
            dut.write("burst 20")
            dut.expect_exact("BURST 1\r\n", timeout=30)
            kept = min(capacity, 20)
            dropped = 20 - kept if capacity else 0
            device.write(f"drain {kept}")
            device.expect_exact(f"EVENTS count={kept} dropped={dropped} payload=1\r\n", timeout=5)
            if capacity == 17:
                device.write("drop")
                device.expect_exact("DROPPED 1\r\n", timeout=30)
            for board in (dut, device):
                board.write("stop")
                board.expect_exact("STOPPED\r\n", timeout=30)
    finally:
        errors = []
        for name, board in (("host", dut), ("device", device)):
            try:
                board.write("stop")
                board.expect_exact("STOPPED\r\n", timeout=30)
            except Exception as exc:
                errors.append(f"{name}: {exc}")
        assert not errors, "USB stop failed: " + "; ".join(errors)
