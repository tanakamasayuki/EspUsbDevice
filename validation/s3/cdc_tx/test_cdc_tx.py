"""CDC Print backpressure, TX deadlines, worker safety and real completion."""
import pytest


@pytest.mark.parametrize("auto_flush", [0, 1])
def test_cdc_tx(dut, peers, auto_flush):
    device = peers["device"]

    def command(board, text, reply, timeout=15):
        board.write(text)
        board.expect_exact(reply + "\r\n", timeout=timeout)

    try:
        command(device, f"start {auto_flush}", "STARTED 1", 30)
        command(dut, "start", "HOST_READY 1", 30)
        for dtr in (1, 0):
            if not dtr:
                command(dut, "dtr0", "DTR_CLEARED 1")
            command(dut, "arm", "ARMED")
            command(device, "print", f"PRINT ok=1 bytes=4097 free=64 dtr={dtr}")
            command(dut, "check 4097", "RECEIVED ok=1 bytes=4097 bad=0")
        for deadline in (0, 60):
            command(dut, "arm", "ARMED")
            command(dut, "pause", "PAUSED 1")
            device.write(f"blocked {deadline}")
            count = 128 if deadline else 64
            match = device.expect(fr"BLOCKED ok=1 bytes={count} elapsed=(\d+) flush=(\d+)\r\n", timeout=5)
            for elapsed in match.groups():
                assert deadline <= int(elapsed) < deadline + 150
            if deadline == 0:
                device.write("dmawait")
                match = device.expect(r"DMA_WAIT ok=1 elapsed=(\d+)\r\n", timeout=5)
                assert 60 <= int(match.group(1)) < 210
            command(dut, "resume", "RESUMED 1")
            command(device, "flushed", "FLUSHED 1")
            command(dut, f"check {count}", f"RECEIVED ok=1 bytes={count} bad=0")
        command(dut, "arm", "ARMED")
        command(dut, "worker", "TRIGGERED 1")
        device.write("worker")
        match = device.expect(r"WORKER ok=1 bytes=64 elapsed=(\d+)\r\n", timeout=5)
        assert int(match.group(1)) < 50
        command(dut, "check 64", "RECEIVED ok=1 bytes=64 bad=0")
        command(device, "flushed", "FLUSHED 1")
        command(dut, "arm", "ARMED")
        command(dut, "pause", "PAUSED 1")
        device.write("stopduring")
        match = device.expect(r"STOP_DURING ok=1 bytes=128 elapsed=(\d+)\r\n", timeout=10)
        assert int(match.group(1)) < 300
        command(dut, "resume", "RESUMED 1")
    finally:
        errors = []
        for name, board in (("host", dut), ("device", device)):
            try:
                command(board, "stop", "STOPPED", 30)
            except Exception as exc:
                errors.append(f"{name}: {exc}")
        assert not errors, "USB stop failed: " + "; ".join(errors)


def test_cdc_tx_disabled(dut, peers):
    device = peers["device"]
    try:
        device.write("start disabled")
        device.expect_exact("STARTED 1\r\n", timeout=30)
        dut.write("start")
        dut.expect_exact("HOST_READY 1\r\n", timeout=30)
        device.write("disabled")
        match = device.expect(r"DISABLED ok=1 elapsed=(\d+)\r\n", timeout=5)
        assert int(match.group(1)) < 50
    finally:
        errors = []
        for name, board in (("host", dut), ("device", device)):
            try:
                board.write("stop")
                board.expect_exact("STOPPED\r\n", timeout=30)
            except Exception as exc:
                errors.append(f"{name}: {exc}")
        assert not errors, "USB stop failed: " + "; ".join(errors)
