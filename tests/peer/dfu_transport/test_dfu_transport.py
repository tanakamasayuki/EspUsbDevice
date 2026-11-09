"""Independent DFU interfaces, runtime resources and a synchronous RAM backend."""


def test_dfu_transport(dut, peers):
    device = peers["device"]
    try:
        device.write("metadata")
        r = device.expect(r"METADATA ok=1 before=(\d+) after=(\d+)\r\n", timeout=30)
        assert r.group(1) == r.group(2), "DFU resource storage leaked heap"
        for size, external in ((64, 0), (129, 1), (4097, 0), (65535, 1)):
            device.write(f"start {size} {external}")
            device.expect_exact("STARTED 1\r\n", timeout=30)
            dut.write(f"start {size}")
            dut.expect_exact("HOST_READY 1\r\n", timeout=30)
            for _ in range(2):
                dut.write("roundtrip")
                r = dut.expect(r"ROUNDTRIP 1 bytes=(\d+) blocks=(\d+)\r\n", timeout=30)
                device.write("stats")
                device.expect_exact(f"COUNTS 0 written={int(r.group(1))} ", timeout=5)
                device.expect(r"downloads=\d+ manifests=\d+ aborts=0 errors=0 bad=0\r\n", timeout=5)
            for command in ("errors", "runtime", "multi"):
                dut.write(command)
                dut.expect_exact(command.upper() + " 1\r\n", timeout=30)
            for flag, command in ((1, "write_error"), (2, "manifest_error")):
                device.write(f"inject {flag}")
                device.expect_exact("INJECTED\r\n", timeout=5)
                dut.write(command)
                dut.expect_exact(command.upper() + " 1\r\n", timeout=30)
            device.write("inject 0")
            device.expect_exact("INJECTED\r\n", timeout=5)
            dut.write("open")
            dut.expect_exact("OPENED 1\r\n", timeout=10)
            for board in (dut, device):
                board.write("stop")
                board.expect_exact("STOPPED\r\n", timeout=30)
            device.write("stats")
            device.expect(r"COUNTS 0 written=0 downloads=\d+ manifests=\d+ aborts=5 errors=\d+ bad=0\r\n", timeout=5)
            device.expect(r"COUNTS 1 written=0 downloads=1 manifests=1 aborts=0 errors=0 bad=0\r\n", timeout=5)
            device.expect_exact("DETACH 1\r\n", timeout=5)
        device.write("start 64 0 0")
        device.expect_exact("STARTED 1\r\n", timeout=30)
        dut.write("start 64")
        dut.expect_exact("HOST_READY 1\r\n", timeout=30)
        dut.write("manifest_wait")
        dut.expect_exact("MANIFEST_WAIT 1\r\n", timeout=30)
        for board in (dut, device):
            board.write("stop")
            board.expect_exact("STOPPED\r\n", timeout=30)
        device.write("stats")
        device.expect_exact("COUNTS 0 written=0 downloads=1 manifests=1 aborts=0 errors=0 bad=0\r\n", timeout=5)
    finally:
        errors = []
        for name, board in (("host", dut), ("device", device)):
            try:
                board.write("stop")
                board.expect_exact("STOPPED\r\n", timeout=30)
            except Exception as exc:
                errors.append(f"{name}: {exc}")
        assert not errors, "USB stop failed: " + "; ".join(errors)
