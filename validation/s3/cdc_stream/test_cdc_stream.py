"""Two independent CDC ports and Vendor on the shared runtime bulk driver."""


def test_cdc_stream(dut, peers):
    device = peers["device"]
    try:
        for capacities in ((64, 64, 128, 128), (1024, 2048, 512, 1024), (97, 131, 149, 197)):
            device.write("configure " + " ".join(map(str, capacities)))
            device.expect_exact("CONFIGURED first=0 second=0\r\n", timeout=5)
            device.write("start")
            device.expect_exact("DEVICE_STARTED\r\n", timeout=30)
            dut.write("start")
            dut.expect_exact("HOST_READY 1 ports=2\r\n", timeout=30)
            device.write("capacities")
            device.expect_exact("CAPACITIES " + " ".join(map(str, capacities)) + "\r\n", timeout=5)
            device.write("mutate")
            device.expect_exact("MUTATION_REJECTED 1\r\n", timeout=5)
            device.write("configure 512 512 512 512")
            device.expect_exact("CONFIGURED first=259 second=259\r\n", timeout=5)
            for port in range(2):
                for length in (1, 63, 64, 65, 127, 128, 129):
                    device.write(f"expect {port} {length}")
                    device.expect_exact(f"ARMED {port} {length}\r\n", timeout=5)
                    dut.write(f"exchange {port} {length}")
                    dut.expect_exact(f"EXCHANGE port={port} length={length} actual={length} ok=1\r\n", timeout=10)
                    device.expect_exact(f"ECHOED {port} {length}\r\n", timeout=5)
            for port, baud in ((0, 57600), (1, 230400)):
                dut.write(f"line {port} {baud}")
                dut.expect_exact("LINE_SET 1\r\n", timeout=10)
            device.write("state")
            for port, baud in ((0, 57600), (1, 230400)):
                match = device.expect(fr"PORT {port} mounted=1 connected=1 baud={baud} stop=2 parity=2 bits=7 dtr=1 rts=1 coding_events=(\d+) state_events=(\d+)\r\n", timeout=5)
                assert int(match.group(1)) > 0 and int(match.group(2)) > 0
            for board in (dut, device):
                board.write("stop")
                board.expect_exact("STOPPED\r\n", timeout=30)
            device.write("capacities")
            device.expect_exact("CAPACITIES 0 0 0 0\r\n", timeout=5)
        device.write("borrowed")
        device.expect_exact("BORROWED 0\r\n", timeout=5)
        device.write("start")
        device.expect_exact("DEVICE_STARTED\r\n", timeout=30)
        dut.write("start")
        dut.expect_exact("HOST_READY 1 ports=2\r\n", timeout=30)
        device.write("capacities")
        device.expect_exact("CAPACITIES 0 0 149 197\r\n", timeout=5)
        for length in (1, 63, 64, 65, 127, 128, 129):
            device.write(f"expect 0 {length}")
            device.expect_exact(f"ARMED 0 {length}\r\n", timeout=5)
            dut.write(f"exchange 0 {length}")
            dut.expect_exact(f"EXCHANGE port=0 length={length} actual={length} ok=1\r\n", timeout=10)
            device.expect_exact(f"ECHOED 0 {length}\r\n", timeout=5)
        for board in (dut, device):
            board.write("stop")
            board.expect_exact("STOPPED\r\n", timeout=30)
        # Two CDC functions use all four S3 non-control IN endpoints. Switch
        # the graph to one CDC + Vendor + HID to exercise mixed drivers.
        device.write("combined")
        device.expect_exact("COMBINED\r\n", timeout=5)
        device.write("start")
        device.expect_exact("DEVICE_STARTED\r\n", timeout=30)
        dut.write("start 1")
        dut.expect_exact("HOST_READY 1 ports=1\r\n", timeout=30)
        for port in (0, 2):
            for length in (1, 64, 129):
                device.write(f"expect {port} {length}")
                device.expect_exact(f"ARMED {port} {length}\r\n", timeout=5)
                dut.write(f"exchange {port} {length}")
                dut.expect_exact(f"EXCHANGE port={port} length={length} actual={length} ok=1\r\n", timeout=10)
                device.expect_exact(f"ECHOED {port} {length}\r\n", timeout=5)
        device.write("key")
        device.expect_exact("KEY_SENT 1\r\n", timeout=5)
        dut.write("key_result")
        dut.expect_exact("KEY_RESULT pressed=1 released=1\r\n", timeout=10)
    finally:
        errors = []
        for name, board in (("host", dut), ("device", device)):
            try:
                board.write("stop")
                board.expect_exact("STOPPED\r\n", timeout=30)
            except Exception as exc:
                errors.append(f"{name}: {exc}")
        assert not errors, "USB stop failed: " + "; ".join(errors)
