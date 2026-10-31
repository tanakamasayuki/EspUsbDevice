"""Replay old completions across the USB worker event budget."""


def test_cancel_fence(dut, peers, request):
    device = peers["device"]
    mps = 512 if "_hs_" in request.node.name else 64  # bulk MPS of the link
    ready = f"HOST_READY mps_in={mps} mps_out={mps}\r\n"
    try:
        device.write("rx_mode")
        device.expect_exact("RX_MODE\r\n", timeout=5)
        device.write("start")
        device.expect_exact("DEVICE_STARTED\r\n", timeout=30)
        dut.write("start")
        dut.expect_exact(ready, timeout=30)
        device.write(f"rx_arm {3 * mps} {mps}")
        device.expect_exact("RX_ARMED 1 error=0\r\n", timeout=5)
        dut.write(f"send {mps} 0")
        dut.expect_exact(f"SENT {mps} zlp=0 ok=1\r\n", timeout=10)
        device.write("hold")
        device.expect_exact("HELD 1 busy=1\r\n", timeout=5)
        dut.write("prefix")
        dut.expect_exact("PREFIX_OK 1\r\n", timeout=10)
        device.write("fault_cancel")
        device.expect_exact("FAULT_REQUESTED 1\r\n", timeout=5)
        device.write("fault_status")
        device.expect_exact("FAULT_STATUS events=16 full=1 tx=0 rx=0\r\n", timeout=5)
        device.write("tx_wait 1")
        device.expect_exact("TX_WAIT 1\r\n", timeout=5)
        device.write("direct_status")
        device.expect_exact(f"DIRECT_STATUS 1 status=4 requested={2 * mps + 1} actual=0 known=0 pointer=1\r\n", timeout=5)
        device.write("cancel_stale")
        device.expect_exact("CANCEL_STALE tx=259 rx=259\r\n", timeout=5)
        # The cancel callback posts RX immediately: old completions must not
        # return this second buffer before its new data arrives.
        dut.write(f"send {mps - 1} 0")
        dut.expect_exact(f"SENT {mps - 1} zlp=0 ok=1\r\n", timeout=10)
        device.write("rx_chain_result")
        device.expect_exact("RX_CHAIN_RESULT 1\r\n", timeout=5)
        device.write(f"tx_post {mps - 1}")
        device.expect_exact("TX_POSTED 1 error=0\r\n", timeout=5)
        dut.write(f"receive {mps - 1}")
        dut.expect_exact(f"READ_OK {mps - 1}\r\n", timeout=10)
        device.write("tx_wait 2")
        device.expect_exact("TX_WAIT 2\r\n", timeout=5)
    finally:
        errors = []
        for name, board in (("host", dut), ("device", device)):
            try:
                board.write("stop")
                board.expect_exact("STOPPED\r\n", timeout=30)
            except Exception as exc:
                errors.append(f"{name}: {exc}")
        assert not errors, "; ".join(errors)
