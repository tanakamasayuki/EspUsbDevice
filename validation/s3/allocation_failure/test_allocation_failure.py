"""Fail each Vendor/driver-registry acquisition and prove recovery on S3."""


def test_allocation_failure(dut, peers):
    device = peers["device"]
    try:
        device.write("faults")
        result = device.expect(r"FAULTS ok=1 failed=712 recovered=712 before=(\d+) after=(\d+)\r\n", timeout=60)
        assert result.group(1) == result.group(2), "heap leaked across failure/recovery"
        # A final real exchange proves the recovered registry still enumerates.
        device.write("start")
        device.expect_exact("DEVICE_STARTED\r\n", timeout=30)
        dut.write("start")
        dut.expect_exact("HOST_READY mps_in=64 mps_out=64\r\n", timeout=30)
        for length in (1, 64, 129):
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
