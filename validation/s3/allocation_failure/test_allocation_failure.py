"""Fail each Vendor/driver-registry acquisition and prove recovery on S3."""


def test_allocation_failure(dut, peers, request):
    device = peers["device"]
    mps = 512 if "_hs_" in request.node.name else 64  # bulk MPS of the link
    try:
        device.write("faults")
        device.expect(r"AUDIO_SETTINGS ok=1 formats=24 rates=12 failures=14 object=\d+ model=\d+ controls=\d+\r\n", timeout=30)
        device.expect_exact("AUDIO_CLEAR ok=1\r\n", timeout=10)
        planned = 0
        for mode in range(47):
            baseline = device.expect(fr"FAULT_CONFIG mode={mode} points=(\d+)\r\n", timeout=60)
            planned += int(baseline.group(1))
        result = device.expect(r"FAULTS ok=1 failed=(\d+) recovered=(\d+) before=(\d+) after=(\d+)\r\n", timeout=60)
        assert int(result.group(1)) == int(result.group(2)) == planned, "Some measured acquisition points were not exercised"
        assert result.group(3) == result.group(4), "heap leaked across failure/recovery"
        # A final real exchange proves the recovered registry still enumerates.
        device.write("start")
        device.expect_exact("DEVICE_STARTED\r\n", timeout=30)
        dut.write("start")
        dut.expect_exact(f"HOST_READY mps_in={mps} mps_out={mps}\r\n", timeout=30)
        for length in (1, mps, 2 * mps + 1):
            device.write(f"expect {length}")
            device.expect_exact(f"ARMED {length}\r\n", timeout=5)
            dut.write(f"exchange {length}")
            dut.expect_exact(f"ROUNDTRIP_OK {length}\r\n", timeout=10)
            device.expect_exact(f"ECHOED {length}\r\n", timeout=10)
    finally:
        errors = []
        for name, board in (("host", dut), ("device", device)):
            try:
                board.write("stop")
                board.expect_exact("STOPPED\r\n", timeout=30)
            except Exception as exc:
                errors.append(f"{name}: {exc}")
        assert not errors, "USB stop failed: " + "; ".join(errors)
