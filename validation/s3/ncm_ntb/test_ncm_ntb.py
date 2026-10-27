"""Raw NTB validation, independent NCM interfaces and alternate-setting recovery."""


def test_ncm_ntb(dut, peers):
    device = peers["device"]
    try:
        device.write("start")
        device.expect_exact("STARTED 1\r\n", timeout=30)
        dut.write("start")
        dut.expect_exact("HOST_READY 1\r\n", timeout=30)
        for command in ("matrix", "multi", "toggle"):
            dut.write(command)
            dut.expect_exact(command.upper() + " 1\r\n", timeout=60)
        device.write("stats")
        device.expect_exact("STATS 0 tx=43 rx=43 invalid=22\r\n", timeout=5)
        device.expect_exact("STATS 1 tx=1 rx=1 invalid=0\r\n", timeout=5)
        device.expect_exact("REJECTED 0\r\n", timeout=5)
        device.write("ip")
        device.expect_exact("IP_PAIR 1\r\n", timeout=20)
    finally:
        errors = []
        for name, board in (("host", dut), ("device", device)):
            try:
                board.write("stop")
                board.expect_exact("STOPPED\r\n", timeout=30)
            except Exception as exc:
                errors.append(f"{name}: {exc}")
        assert not errors, "USB stop failed: " + "; ".join(errors)
