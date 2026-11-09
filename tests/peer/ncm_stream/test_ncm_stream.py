"""Runtime NCM resources, NTB aggregation and native host interoperability."""


def test_ncm_stream(dut, peers):
    device = peers["device"]
    try:
        device.write("metadata")
        r = device.expect(r"METADATA ok=1 before=(\d+) after=(\d+)\r\n", timeout=30)
        assert r.group(1) == r.group(2), "NCM resources leaked heap"
        for capacity, buffers, external in ((2048, 1, 0), (3200, 2, 1), (8192, 3, 0)):
            device.write(f"start {capacity} {buffers} {external}")
            device.expect_exact("STARTED 1\r\n", timeout=30)
            dut.write("start")
            dut.expect_exact("HOST_READY 1\r\n", timeout=30)
            for length in (14, 63, 64, 65, 72, 127, 128, 129, 1500, 1514):
                dut.write(f"exchange {length}")
                dut.expect_exact(f"EXCHANGE {length} 1\r\n", timeout=10)
            device.write("burst 4 129")
            device.expect_exact("BURST 1\r\n", timeout=5)
            dut.write("burst 4 129")
            dut.expect_exact("RECEIVED 1\r\n", timeout=10)
            dut.write("limits")
            dut.expect_exact("LIMITS 1\r\n", timeout=15)
            dut.write("exchange 1514")
            dut.expect_exact("EXCHANGE 1514 1\r\n", timeout=10)
            device.write("stats")
            device.expect(r"STATS received=11 rejected=0 tx=15 rx=11 invalid=0 link=1\r\n", timeout=5)
            device.write("network")
            device.expect_exact("NETWORK 1\r\n", timeout=20)
            dut.write("exchange 1514")
            dut.expect_exact("EXCHANGE 1514 1\r\n", timeout=10)
            device.write("ip_start")
            device.expect_exact("IP_READY 1\r\n", timeout=20)
            dut.write("tcp")
            dut.expect(r"TCP 1 bytes=8193 ip=192\.168\.7\.\d+\r\n", timeout=40)
            device.write("ip_stop")
            device.expect_exact("IP_STOPPED 1\r\n", timeout=10)
            dut.write("exchange 129")
            dut.expect_exact("EXCHANGE 129 1\r\n", timeout=10)
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
