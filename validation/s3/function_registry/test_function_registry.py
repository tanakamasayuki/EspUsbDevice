"""More than six Functions, rebuild/rollback, and seven real HID report IDs."""


def test_function_registry(dut, peers):
    device = peers["device"]
    try:
        device.write("metadata")
        result = device.expect(r"METADATA ok=1 before=(\d+) after=(\d+)\r\n", timeout=30)
        assert result.group(1) == result.group(2), "registry growth/removal leaked heap"
        device.write("start")
        device.expect_exact("REGISTRY_STARTED 1 reports=161\r\n", timeout=30)
        dut.write("start")
        dut.expect_exact("HOST_READY 1\r\n", timeout=30)
        device.write("active_reject")
        device.expect_exact("ACTIVE_REJECT 1\r\n", timeout=5)
        device.write("send")
        device.expect_exact("REPORTS_SENT 1\r\n", timeout=10)
        dut.write("reports")
        dut.expect_exact("REPORTS mask=254 invalid=0\r\n", timeout=5)
    finally:
        errors = []
        for name, board in (("host", dut), ("device", device)):
            try:
                board.write("stop")
                board.expect_exact("STOPPED\r\n", timeout=30)
            except Exception as exc:
                errors.append(f"{name}: {exc}")
        assert not errors, "USB stop failed: " + "; ".join(errors)
