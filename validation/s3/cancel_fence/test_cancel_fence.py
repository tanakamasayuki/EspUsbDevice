"""Replay old completions across the USB worker event budget."""


def test_cancel_fence(dut, peers):
    device = peers["device"]
    try:
        device.write("rx_mode")
        device.expect_exact("RX_MODE\r\n", timeout=5)
        device.write("start")
        device.expect_exact("DEVICE_STARTED\r\n", timeout=30)
        dut.write("start")
        dut.expect_exact("HOST_READY mps_in=64 mps_out=64\r\n", timeout=30)
        device.write("rx_arm 192 64")
        device.expect_exact("RX_ARMED 1 error=0\r\n", timeout=5)
        dut.write("send 64 0")
        dut.expect_exact("SENT 64 zlp=0 ok=1\r\n", timeout=10)
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
        device.expect_exact("DIRECT_STATUS 1 status=4 requested=129 actual=0 known=0 pointer=1\r\n", timeout=5)
        device.write("cancel_stale")
        device.expect_exact("CANCEL_STALE tx=259 rx=259\r\n", timeout=5)
        # The cancel callback posts RX immediately: old completions must not
        # return this second buffer before its new data arrives.
        dut.write("send 63 0")
        dut.expect_exact("SENT 63 zlp=0 ok=1\r\n", timeout=10)
        device.write("rx_chain_result")
        device.expect_exact("RX_CHAIN_RESULT 1\r\n", timeout=5)
        device.write("tx_post 63")
        device.expect_exact("TX_POSTED 1 error=0\r\n", timeout=5)
        dut.write("receive 63")
        dut.expect_exact("READ_OK 63\r\n", timeout=10)
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
