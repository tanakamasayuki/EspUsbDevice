"""Deterministic cancellation/reset/end races on the real S3 pair."""


def test_cancel_lifecycle(dut, peers):
    device = peers["device"]
    tx_count = rx_count = 0
    try:
        device.write("rx_mode")
        device.expect_exact("RX_MODE\r\n", timeout=5)
        for _ in range(3):
            for scenario in ("reset", "end_before_abort", "end_before_fence"):
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
                command = "fault_park_cancelled" if scenario == "end_before_fence" else "fault_park"
                device.write(command)
                device.expect_exact("LIFECYCLE_PARKED 1\r\n", timeout=5)
                if scenario != "end_before_fence":
                    device.write("cancel_both")
                    device.expect_exact("CANCEL_REQUEST tx=0 rx=0\r\n", timeout=5)
                if scenario == "reset":
                    dut.write("stop")
                    dut.expect_exact("STOPPED\r\n", timeout=30)
                    # Wait until the real DCD ISR has queued a bus reset before
                    # releasing the worker, so reset precedes the abort job.
                    dut.write("start")
                    device.write("fault_reset_release")
                    device.expect_exact("LIFECYCLE_RESET observed=1 parked=1\r\n", timeout=10)
                    dut.expect_exact("HOST_READY mps_in=64 mps_out=64\r\n", timeout=30)
                    status = 1  # BusReset
                else:
                    device.write("fault_end_release")
                    device.expect_exact("LIFECYCLE_END parked=1 stopping=1 done=1 error=0\r\n", timeout=10)
                    status = 2  # Stopped, including queued but unexecuted fences
                tx_count += 1
                rx_count += 1
                device.write(f"tx_wait {tx_count}")
                device.expect_exact(f"TX_WAIT {tx_count}\r\n", timeout=5)
                device.write("direct_status")
                device.expect_exact(f"DIRECT_STATUS {tx_count} status={status} requested=129 actual=0 known=0 pointer=1\r\n", timeout=5)
                device.write("rx_result")
                device.expect_exact(f"RX_RESULT {rx_count} status={status} requested=192 actual=0 known=0 pointer=1 guards=1 payload=1\r\n", timeout=5)
                if scenario != "reset":
                    dut.write("stop")
                    dut.expect_exact("STOPPED\r\n", timeout=30)
                    device.write("start")
                    device.expect_exact("DEVICE_STARTED\r\n", timeout=30)
                    dut.write("start")
                    dut.expect_exact("HOST_READY mps_in=64 mps_out=64\r\n", timeout=30)
                # A returned old loan/id cannot disturb either new direction.
                device.write("rx_arm 64 63")
                device.expect_exact("RX_ARMED 1 error=0\r\n", timeout=5)
                device.write("tx_post 63")
                device.expect_exact("TX_POSTED 1 error=0\r\n", timeout=5)
                device.write("cancel_stale")
                device.expect_exact("CANCEL_STALE tx=259 rx=259\r\n", timeout=5)
                dut.write("send 63 0")
                dut.expect_exact("SENT 63 zlp=0 ok=1\r\n", timeout=10)
                rx_count += 1
                device.write("rx_result")
                device.expect_exact(f"RX_RESULT {rx_count} status=0 requested=64 actual=63 known=1 pointer=1 guards=1 payload=1\r\n", timeout=5)
                dut.write("receive 63")
                dut.expect_exact("READ_OK 63\r\n", timeout=10)
                tx_count += 1
                device.write(f"tx_wait {tx_count}")
                device.expect_exact(f"TX_WAIT {tx_count}\r\n", timeout=5)
                device.write("direct_status")
                device.expect_exact(f"DIRECT_STATUS {tx_count} status=0 requested=63 actual=63 known=1 pointer=1\r\n", timeout=5)
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
