"""Runtime MIDI packet queues, cables, callbacks, direction modes and peers."""


def test_midi_stream(dut, peers):
    device = peers["device"]
    try:
        device.write("metadata")
        result = device.expect(r"METADATA ok=1 before=(\d+) after=(\d+)\r\n", timeout=30)
        assert result.group(1) == result.group(2), "MIDI configuration leaked heap"
        for mode, rx, tx, cables in ((0, 1, 1, 1), (1, 37, 43, 16), (2, 3, 5, 1),
                                      (3, 0, 128, 1), (4, 0, 1, 1), (5, 1, 0, 1),
                                      (6, 1, 1, 1), (7, 1, 1, 1), (8, 1, 1, 1), (9, 1, 1, 1)):
            device.write(f"start {mode}")
            in_cables = 16 if mode == 9 else cables
            device.expect_exact(f"STARTED 1 rx={rx} tx={tx} in={in_cables} out={cables}\r\n", timeout=30)
            dut.write(f"start {in_cables} {cables}")
            dut.expect_exact("HOST_READY 1\r\n", timeout=30)
            if mode == 8:
                device.write("batch")
                device.expect_exact("BATCH 1\r\n", timeout=5)
                dut.write("quiet")
                dut.expect_exact("QUIET 1\r\n", timeout=5)
                device.write("flush")
                device.expect_exact("FLUSHED\r\n", timeout=5)
                dut.write("wait 1")
                dut.expect_exact("RECEIVED 1 bad=0\r\n", timeout=10)
                for board in (dut, device):
                    board.write("stop")
                    board.expect_exact("STOPPED\r\n", timeout=30)
                continue
            events = 16 if mode == 3 else 64
            if mode == 0:
                device.write("rejected")
                device.expect_exact("TX_REJECTED 1\r\n", timeout=5)
                dut.write(f"send {events}")
                dut.expect_exact("SENT 1\r\n", timeout=5)
                device.write("pending")
                device.expect_exact("STAGED 1\r\n", timeout=5)
            if mode not in (3, 4):
                device.write(f"echo {events}")
                device.expect_exact(f"ARMED {events}\r\n", timeout=5)
            if mode not in (0, 4):
                dut.write(f"send {events}")
                dut.expect_exact("SENT 1\r\n", timeout=5)
            elif mode == 4:
                device.write(f"send {events}")
            if mode != 5:
                dut.write(f"wait {events}")
                dut.expect_exact(f"RECEIVED {events} bad=0\r\n", timeout=10)
            if mode == 3:
                device.write("callback_result")
                device.expect_exact(f"CALLBACK {events} payload=1\r\n", timeout=5)
            else:
                device.expect_exact(f"DONE {events} payload=1\r\n", timeout=10)
            if mode == 0:
                dut.write("clear")
                dut.expect_exact("CLEARED\r\n", timeout=5)
                device.write("echo 1")
                device.expect_exact("ARMED 1\r\n", timeout=5)
                dut.write("invalid")
                dut.expect_exact("INVALID_SENT 1\r\n", timeout=5)
                dut.write("wait 1")
                dut.expect_exact("RECEIVED 1 bad=0\r\n", timeout=10)
                device.expect_exact("DONE 1 payload=1\r\n", timeout=10)
                device.write("errors")
                device.expect_exact("ERRORS 3\r\n", timeout=5)
                dut.write("parallel")
                dut.expect_exact("PARALLEL_ARMED\r\n", timeout=5)
                device.write("parallel")
                device.expect_exact("PARALLEL_STARTED 1\r\n", timeout=5)
                dut.write("wait 200")
                dut.expect_exact("RECEIVED 200 bad=0\r\n", timeout=10)
                device.write("parallel_result")
                device.expect_exact("PARALLEL_DONE 1\r\n", timeout=5)
            if mode == 2:
                device.write("capacities")
                device.expect_exact("CAPACITY rx=3 tx=5 guards=1\r\n", timeout=5)
            if mode == 6:
                dut.write("second")
                dut.expect_exact("SECOND_SENT 1\r\n", timeout=5)
                device.write("second")
                device.expect_exact("SECOND 1\r\n", timeout=10)
                dut.write("second_read")
                dut.expect_exact("SECOND_RECEIVED 1\r\n", timeout=10)
            if mode == 7:
                dut.write("helper")
                dut.expect_exact("HELPER_ARMED\r\n", timeout=5)
                device.write("helpers")
                device.expect_exact("HELPERS 1\r\n", timeout=5)
                dut.write("wait 1")
                dut.expect_exact("RECEIVED 1 bad=0\r\n", timeout=10)
                dut.write("composite")
                dut.expect_exact("COMPOSITE_SENT 1\r\n", timeout=5)
                device.write("composite")
                device.expect_exact("COMPOSITE 1\r\n", timeout=10)
                dut.write("composite_result")
                dut.expect_exact("COMPOSITE_RECEIVED 1\r\n", timeout=10)
            if mode in (7, 9):
                dut.write(f"semantics {in_cables - 1}")
                dut.expect_exact("SEMANTICS_ARMED\r\n", timeout=5)
                device.write("semantics")
                device.expect_exact("SEMANTICS 1\r\n", timeout=10)
                dut.write("wait 15")
                dut.expect_exact("RECEIVED 15 bad=0\r\n", timeout=10)
            if mode == 0:
                dut.write("ignore")
                dut.expect_exact("IGNORING\r\n", timeout=5)
                device.write("end_race")
                device.expect_exact("END_RACE 1\r\n", timeout=10)
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
