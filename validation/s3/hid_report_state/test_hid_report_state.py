"""Per-ID GET_REPORT snapshots, concurrent Feature updates, idle and reset."""


def test_hid_report_state(dut, peers):
    device = peers["device"]
    try:
        device.write("start")
        device.expect_exact("STARTED 1 small=6 large=1\r\n", timeout=30)
        dut.write("start")
        dut.expect_exact("HOST_READY 1\r\n", timeout=30)
        dut.write("initial")
        dut.expect_exact("STATE 1\r\n", timeout=10)
        device.write("publish")
        device.expect_exact("PUBLISHED 1\r\n", timeout=5)
        dut.write("inputs")
        dut.expect_exact("INPUT_STATE 1\r\n", timeout=10)
        dut.write("put")
        dut.expect_exact("PUT 1\r\n", timeout=10)
        device.write("events")
        device.expect_exact("EVENTS output=2 feature=2\r\n", timeout=5)
        device.write("feature")
        device.expect_exact("FEATURE_UPDATED 1\r\n", timeout=5)
        dut.write("feature")
        dut.expect_exact("FEATURE_STATE 1\r\n", timeout=10)
        device.write("stress")
        device.expect_exact("STRESS 1\r\n", timeout=5)
        dut.write("stress")
        dut.expect_exact("COHERENT 1 reads=200\r\n", timeout=15)
        device.write("stress_stop")
        device.expect_exact("STRESS_STOPPED\r\n", timeout=5)
        device.write("feature")
        device.expect_exact("FEATURE_UPDATED 1\r\n", timeout=5)
        dut.write("idle")
        dut.expect(r"IDLE ok=1 small=\d+ large=\d+ avg_small_us=\d+ avg_large_us=\d+\r\n", timeout=10)
        dut.expect_exact("ISOLATION 1\r\n", timeout=5)
        dut.expect_exact("QUIET 1\r\n", timeout=5)
        for _ in range(3):
            dut.write("suspend_idle")
            dut.expect_exact("IDLE_SUSPENDED 1\r\n", timeout=10)
            device.write("suspended")
            device.expect_exact("SUSPEND_REJECTED 1\r\n", timeout=5)
            dut.write("resume_idle")
            dut.expect(r"IDLE_RESUMED ok=1 small=\d+ large=\d+\r\n", timeout=10)
            dut.expect_exact("RESUME_QUIET 1\r\n", timeout=5)
            device.write("resumed")
            device.expect_exact("RESUMED 1\r\n", timeout=5)
        dut.write("reconnect")
        dut.expect_exact("HOST_READY 1\r\n", timeout=30)
        dut.write("reset_state")
        dut.expect_exact("STATE 1\r\n", timeout=10)
    finally:
        errors = []
        for name, board in (("host", dut), ("device", device)):
            try:
                board.write("stop")
                board.expect_exact("STOPPED\r\n", timeout=30)
            except Exception as exc:
                errors.append(f"{name}: {exc}")
        assert not errors, "USB stop failed: " + "; ".join(errors)
