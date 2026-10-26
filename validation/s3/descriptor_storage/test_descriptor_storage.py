"""Size-before-write bounds, large descriptors, optional storage, real USB."""


def test_descriptor_storage(dut, peers):
    device = peers["device"]
    try:
        device.write("metadata")
        result = device.expect(r"WRITERS ok=1 cases=44 before=(\d+) after=(\d+)\r\n", timeout=30)
        assert result.group(1) == result.group(2), "descriptor failure/lazy HS leaked heap"
        device.write("vendor")
        device.expect_exact("VENDOR_STARTED ok=1 config=846 ms=516 bos=33 functions=3 properties=3 revisions=3 mask=7\r\n", timeout=30)
        dut.write("vendor")
        dut.expect_exact("HOST_READY 1\r\n", timeout=30)
        for length in (1, 63, 64, 65, 127, 128, 129):
            device.write(f"expect {length}")
            device.expect_exact(f"ARMED {length}\r\n", timeout=5)
            dut.write(f"exchange {length}")
            dut.expect_exact(f"ROUNDTRIP {length} ok=1\r\n", timeout=10)
            device.expect_exact(f"ECHOED {length}\r\n", timeout=5)
        for board in (dut, device):
            board.write("stop")
            board.expect_exact("STOPPED\r\n", timeout=30)
        device.write("hid")
        device.expect_exact("HID_STARTED ok=1 report=366 bos=0 ms=0\r\n", timeout=30)
        dut.write("hid")
        dut.expect_exact("HOST_READY 1\r\n", timeout=30)
        device.write("send")
        device.expect_exact("REPORTS_SENT 1\r\n", timeout=10)
        dut.write("reports")
        dut.expect_exact("REPORTS mask=6 invalid=0\r\n", timeout=5)
    finally:
        errors = []
        for name, board in (("host", dut), ("device", device)):
            try:
                board.write("stop")
                board.expect_exact("STOPPED\r\n", timeout=30)
            except Exception as exc:
                errors.append(f"{name}: {exc}")
        assert not errors, "USB stop failed: " + "; ".join(errors)
