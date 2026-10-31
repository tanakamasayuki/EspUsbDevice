"""Exercise borrowed transfers across 16-bit boundaries with bounded DMA storage."""


def test_transfer_limits(dut, peers, request):
    device = peers["device"]
    mps = 512 if "_hs_" in request.node.name else 64
    partial = 131072 - mps
    try:
        device.write("start")
        device.expect_exact("DEVICE_STARTED\r\n", timeout=30)
        dut.write("start")
        dut.expect_exact("HOST_READY\r\n", timeout=30)
        device.write("limits")
        limits = device.expect(r"LIMITS tx=(\d+) rx=(\d+) capacity=131072 allocated=1\r\n", timeout=5)
        tx_limit, rx_limit = int(limits.group(1)), int(limits.group(2))
        count = 0
        for length in (65536, 65537, 131072, 8127, 8128, 8129, 16255, 16256, 16257, 65408, 65471, 65472, 65535, 131007, 131008, 131071):
            # Each large TX is followed by one byte to expose a leftover ZLP.
            for terminate in (0, 1):
                for n in (length, 1):
                    device.write(f"tx {n} {terminate}")
                    device.expect_exact("POSTED 1 error=0\r\n", timeout=5)
                    dut.write(f"receive {n} {terminate}")
                    dut.expect_exact(f"TRANSFER length={n} actual={n} ok=1\r\n", timeout=15)
                    count += 1
                    device.write("result")
                    device.expect_exact(f"RESULT count={count} ok=1 guards=1 payload=1\r\n", timeout=5)
            device.write(f"rx 131072 {length}")
            device.expect_exact("POSTED 1 error=0\r\n", timeout=5)
            dut.write(f"send {length} 0")
            dut.expect_exact(f"TRANSFER length={length} actual={length} ok=1\r\n", timeout=15)
            if length % mps == 0 and length < 131072:
                # Full packets below the posted capacity need a separate ZLP.
                # Use the existing Host API through a dedicated command below.
                dut.write("zlp")
                dut.expect_exact("ZLP_SENT 1\r\n", timeout=5)
            count += 1
            device.write("result")
            device.expect_exact(f"RESULT count={count} ok=1 guards=1 payload=1\r\n", timeout=5)
        assert tx_limit >= 131072 and rx_limit >= 131072
        # Keep the last packet outstanding. Even the largest uint16 hardware
        # arm must have completed more than 65535 bytes by this point. Reuse
        # each endpoint. Returning the moved DMA pointer must not lose the loan.
        for direction in ("tx", "rx"):
            device.write(f"{direction} 131072 0")
            device.expect_exact("POSTED 1 error=0\r\n", timeout=5)
            if direction == "tx":
                dut.write(f"receive {partial} 0 131072")
            else:
                dut.write(f"send {partial} 0")
            dut.expect_exact(f"TRANSFER length={partial} actual={partial} ok=1\r\n", timeout=10)
            device.write(f"cancel {direction}")
            device.expect_exact("CANCELLED_REQUEST 0\r\n", timeout=5)
            count += 1
            device.write("result")
            device.expect_exact(f"RESULT count={count} ok=1 guards=1 payload=1\r\n", timeout=5)
            device.write("tx 1 0" if direction == "tx" else f"rx {mps} 63")
            device.expect_exact("POSTED 1 error=0\r\n", timeout=5)
            dut.write("receive 1 0" if direction == "tx" else "send 63 0")
            n = 1 if direction == "tx" else 63
            dut.expect_exact(f"TRANSFER length={n} actual={n} ok=1\r\n", timeout=10)
            count += 1
            device.write("result")
            device.expect_exact(f"RESULT count={count} ok=1 guards=1 payload=1\r\n", timeout=5)
        # A wide active loan is returned exactly once by real bus reset/end.
        for boundary in ("reset", "end"):
            for direction in ("tx", "rx"):
                device.write(f"{direction} 131072 0")
                device.expect_exact("POSTED 1 error=0\r\n", timeout=5)
                dut.write(f"receive {partial} 0 131072" if direction == "tx" else f"send {partial} 0")
                dut.expect_exact(f"TRANSFER length={partial} actual={partial} ok=1\r\n", timeout=10)
                if boundary == "reset":
                    device.write("expect_reset")
                    device.expect_exact("RESET_EXPECTED\r\n", timeout=5)
                    dut.write("stop")
                    dut.expect_exact("STOPPED\r\n", timeout=30)
                    dut.write("start")
                    dut.expect_exact("HOST_READY\r\n", timeout=30)
                else:
                    device.write("end")
                    device.expect_exact("ENDED 1\r\n", timeout=30)
                count += 1
                device.write("result")
                device.expect_exact(f"RESULT count={count} ok=1 guards=1 payload=1\r\n", timeout=5)
                if boundary == "end":
                    dut.write("stop")
                    dut.expect_exact("STOPPED\r\n", timeout=30)
                    device.write("start")
                    device.expect_exact("DEVICE_STARTED\r\n", timeout=30)
                    dut.write("start")
                    dut.expect_exact("HOST_READY\r\n", timeout=30)
                device.write("tx 1 0" if direction == "tx" else f"rx {mps} 63")
                device.expect_exact("POSTED 1 error=0\r\n", timeout=5)
                dut.write("receive 1 0" if direction == "tx" else "send 63 0")
                n = 1 if direction == "tx" else 63
                dut.expect_exact(f"TRANSFER length={n} actual={n} ok=1\r\n", timeout=10)
                count += 1
                device.write("result")
                device.expect_exact(f"RESULT count={count} ok=1 guards=1 payload=1\r\n", timeout=5)
    finally:
        errors = []
        for name, board in (("host", dut), ("device", device)):
            try:
                board.write("stop")
                board.expect_exact("STOPPED\r\n", timeout=30)
            except Exception as exc:
                errors.append(f"{name}: {exc}")
        assert not errors, "USB stop failed: " + "; ".join(errors)
