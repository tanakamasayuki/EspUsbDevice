"""BOT thirteen cases, invalid CBW, reset handshake and SCSI sense."""


def test_msc_bot(dut, peers):
    device = peers["device"]
    try:
        device.write("start 64 0")
        device.expect_exact("STARTED 1 capacity=64\r\n", timeout=30)
        dut.write("start")
        dut.expect_exact("HOST_READY 1\r\n", timeout=30)
        for case in range(1, 14):
            dut.write(f"case {case}")
            dut.expect_exact(f"CASE {case} ok=1\r\n", timeout=15)
        for variant in range(6):
            dut.write(f"invalid {variant}")
            dut.expect_exact(f"INVALID_CBW 1 variant={variant}\r\n", timeout=15)
        dut.write("sense")
        dut.expect_exact("SENSE_CHECK 1\r\n", timeout=15)
        dut.write("interface")
        dut.expect_exact("INTERFACE_CHECK 1\r\n", timeout=30)
        device.write("fail")
        device.expect_exact("IO_CHANGED\r\n", timeout=5)
        dut.write("faultread")
        dut.expect_exact("ERROR_SENSE 1\r\n", timeout=15)
        device.write("recover")
        device.expect_exact("IO_CHANGED\r\n", timeout=5)
        dut.write("check")
        dut.expect_exact("READ_RECOVERED 1\r\n", timeout=15)
        device.write("readonly")
        device.expect_exact("MODE_CHANGED\r\n", timeout=5)
        dut.write("protected")
        dut.expect_exact("ERROR_SENSE 1\r\n", timeout=15)
        dut.write("check")
        dut.expect_exact("READ_RECOVERED 1\r\n", timeout=15)
        for board in (dut, device):
            board.write("stop")
            board.expect_exact("STOPPED\r\n", timeout=30)
        device.write("start 64 0 511")
        device.expect_exact("STARTED 1 capacity=64\r\n", timeout=30)
        dut.write("start")
        dut.expect_exact("HOST_READY 1\r\n", timeout=30)
        dut.write("sector 511")
        dut.expect_exact("GEOMETRY_SET\r\n", timeout=5)
        for case in range(1, 14):
            dut.write(f"case {case}")
            dut.expect_exact(f"CASE {case} ok=1\r\n", timeout=15)
        dut.write("guard")
        dut.expect_exact("SECTOR_GUARD 1\r\n", timeout=15)
        for board in (dut, device):
            board.write("stop")
            board.expect_exact("STOPPED\r\n", timeout=30)
        device.write("start 64 4")
        device.expect_exact("STARTED 1 capacity=64\r\n", timeout=30)
        for interface, command in ((1, "second"), (0, "primary")):
            dut.write(f"start {interface}")
            dut.expect_exact("HOST_READY 1\r\n", timeout=30)
            dut.write("interface")
            dut.expect_exact("INTERFACE_CHECK 1\r\n", timeout=30)
            dut.write(command)
            dut.expect_exact("ISOLATED 1\r\n", timeout=15)
            dut.write("stop")
            dut.expect_exact("STOPPED\r\n", timeout=30)
    finally:
        errors = []
        for name, board in (("host", dut), ("device", device)):
            try:
                board.write("stop")
                board.expect_exact("STOPPED\r\n", timeout=30)
            except Exception as exc:
                errors.append(f"{name}: {exc}")
        assert not errors, "USB stop failed: " + "; ".join(errors)
