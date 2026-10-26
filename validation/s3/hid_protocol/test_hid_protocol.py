"""Hold old-format IN in DMA, switch protocol, then read a fresh report."""


def test_hid_protocol(dut, peers):
    device = peers["device"]
    try:
        for function, lengths in (("keyboard", (8, 29)), ("mouse", (3, 4))):
            device.write(function)
            device.expect_exact("STARTED 1\r\n", timeout=30)
            dut.write("start")
            dut.expect_exact("HOST_READY 1\r\n", timeout=30)
            for protocol in (0, 1, 0, 1):
                device.write("send 4")
                device.expect_exact("SENT 1\r\n", timeout=5)
                # With no host IN submission, a second report must be blocked.
                device.write("send 4")
                device.expect_exact("SENT 0\r\n", timeout=5)
                dut.write(f"protocol {protocol}")
                dut.expect_exact("PROTOCOL_SET 1\r\n", timeout=10)
                wrong = (29 if protocol == 0 else 8) if function == "keyboard" else 2
                device.write(f"wrong {wrong}")
                device.expect_exact("REJECTED 1\r\n", timeout=5)
                dut.write("quiet")
                dut.expect_exact("QUIET 1\r\n", timeout=5)
                device.write("send 5")
                device.expect_exact("SENT 1\r\n", timeout=5)
                length = lengths[protocol]
                dut.write(f"result {length} 5 {int(function == 'mouse')}")
                dut.expect_exact(f"RECEIVED 1 bytes={length}\r\n", timeout=10)
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
