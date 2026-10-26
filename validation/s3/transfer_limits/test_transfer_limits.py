"""Exercise actual successful transfers at the current FS DCD length limit."""


def test_transfer_limits(dut, peers):
    device = peers["device"]
    try:
        device.write("start")
        device.expect_exact("DEVICE_STARTED\r\n", timeout=30)
        dut.write("start")
        dut.expect_exact("HOST_READY\r\n", timeout=30)
        device.write("limits")
        device.expect_exact("LIMITS tx=65535 rx=65472 allocated=1\r\n", timeout=5)
        count = 0
        for length in (8127, 8128, 8129, 16255, 16256, 16257, 65408, 65471, 65472, 65535):
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
            if length > 65472:
                continue
            device.write(f"rx 65472 {length}")
            device.expect_exact("POSTED 1 error=0\r\n", timeout=5)
            dut.write(f"send {length} 0")
            dut.expect_exact(f"TRANSFER length={length} actual={length} ok=1\r\n", timeout=15)
            if length % 64 == 0 and length < 65472:
                # Full packets below the posted capacity need a separate ZLP.
                # Use the existing Host API through a dedicated command below.
                dut.write("zlp")
                dut.expect_exact("ZLP_SENT 1\r\n", timeout=5)
            count += 1
            device.write("result")
            device.expect_exact(f"RESULT count={count} ok=1 guards=1 payload=1\r\n", timeout=5)
        # Abort after crossing the first hardware arm, then immediately reuse
        # each endpoint. Returning the moved DMA pointer must not lose the loan.
        for direction in ("tx", "rx"):
            device.write(f"{direction} 65472 0")
            device.expect_exact("POSTED 1 error=0\r\n", timeout=5)
            if direction == "tx":
                dut.write("receive 8192 0 65472")
            else:
                dut.write("send 8192 0")
            dut.expect_exact("TRANSFER length=8192 actual=8192 ok=1\r\n", timeout=10)
            device.write(f"cancel {direction}")
            device.expect_exact("CANCELLED_REQUEST 0\r\n", timeout=5)
            count += 1
            device.write("result")
            device.expect_exact(f"RESULT count={count} ok=1 guards=1 payload=1\r\n", timeout=5)
            device.write("tx 1 0" if direction == "tx" else "rx 64 63")
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
