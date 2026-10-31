"""A fresh S3 pair probe using only the plugin's dut/peers fixtures."""


def test_vendor_roundtrip(dut, peers, request):
    device = peers["device"]
    mps = 512 if "_hs_" in request.node.name else 64  # bulk MPS of the link
    ready = f"HOST_READY mps_in={mps} mps_out={mps}\r\n"
    hold = 2 * mps + 1  # direct TX size of "hold"
    lengths = (1, mps - 1, mps, mps + 1, 2 * mps - 1, 2 * mps, 2 * mps + 1)
    try:
        # Reconfigure the same binary, including small -> large -> small.
        for rx, tx, transfer, automatic in ((1, 1, 1, 1), (64, 64, 64, 1), (2048, 4096, 256, 1), (97, 131, 128, 1),
                                            (64, 128, 64, 0), (64, 64, 64, 1), (97, 131, 128, 1)):
            device.write(f"configure {rx} {tx} {transfer} {automatic}")
            device.expect_exact(f"CONFIG_RESULT 0 rx={rx} tx={tx}\r\n", timeout=5)
            if rx == 97:
                device.write("external")
                device.expect_exact("EXTERNAL_CONFIGURED 1\r\n", timeout=5)
            device.write("configure 0 0 0 0")
            device.expect_exact(f"CONFIG_RESULT 258 rx={rx} tx={tx}\r\n", timeout=5)
            device.write("allocation_failure")
            device.expect_exact("ALLOCATION_ROLLBACK_OK\r\n", timeout=10)
            device.write("start")
            device.expect_exact("DEVICE_STARTED\r\n", timeout=30)
            dut.write("start")
            dut.expect_exact(ready, timeout=30)
            device.write("capacities")
            device.expect_exact(f"CAPACITIES {rx} {tx}\r\n", timeout=5)
            device.write("start_again")
            device.expect_exact("START_AGAIN_REJECTED 1\r\n", timeout=5)
            device.write("configure 512 512 512 1")
            device.expect_exact(f"CONFIG_RESULT 259 rx={rx} tx={tx}\r\n", timeout=5)
            if not automatic:
                device.write("batch_probe")
                device.expect_exact("BATCH_STAGED 1\r\n", timeout=5)
                device.write("flush")
                device.expect_exact("FLUSHED\r\n", timeout=5)
                dut.write("receive 1")
                dut.expect_exact("READ_OK 1\r\n", timeout=10)

            cases = [(n, hold, not automatic) for n in lengths]
            cases += [(n, chunk, True) for n in (mps, hold) for chunk in (1, 17)]
            for length, chunk, flush in cases:
                device.write(f"expect {length} {chunk} {int(flush)}")
                device.expect_exact(f"ARMED {length}\r\n", timeout=5)
                dut.write(f"exchange {length}")
                dut.expect_exact(f"ROUNDTRIP_OK {length}\r\n", timeout=10)
                device.expect_exact(f"ECHOED {length}\r\n", timeout=10)
            for board in (dut, device):
                board.write("stop")
                board.expect_exact("STOPPED\r\n", timeout=30)
            device.write("capacities")
            device.expect_exact("CAPACITIES 0 0\r\n", timeout=5)
            if rx == 97:
                device.write("canaries")
                device.expect_exact("CANARIES 1\r\n", timeout=5)

        device.write("cycles")
        result = device.expect(r"CYCLES 1 before=(\d+) after=(\d+)\r\n", timeout=60)
        assert result.group(1) == result.group(2), "heap changed after 100 begin/end cycles"

        # No copied TX ring/staging buffer is allocated for this phase.
        device.write("direct_mode")
        device.expect_exact("DIRECT_MODE\r\n", timeout=5)
        device.write("configure 64 0 64 1")
        device.expect_exact("CONFIG_RESULT 0 rx=64 tx=0\r\n", timeout=5)
        device.write("start")
        device.expect_exact("DEVICE_STARTED\r\n", timeout=30)
        dut.write("start")
        dut.expect_exact(ready, timeout=30)
        device.write("direct_invalid")
        assert int(device.expect(r"DIRECT_INVALID 1 max=(\d+)\r\n", timeout=5).group(1)) >= 131072
        device.write("callback_mutation")
        device.expect_exact("CALLBACK_MUTATION_REJECTED 1\r\n", timeout=5)
        for length in lengths:
            for terminate in (0, 1):
                device.write(f"expect {length} {hold} 0 {terminate}")
                device.expect_exact(f"ARMED {length}\r\n", timeout=5)
                dut.write(f"exchange {length} {terminate}")
                dut.expect_exact(f"ROUNDTRIP_OK {length}\r\n", timeout=10)
                device.expect_exact(f"ECHOED {length}\r\n", timeout=10)

        device.write("hold")
        device.expect_exact("HELD 1 busy=1\r\n", timeout=5)
        dut.write("stop")
        dut.expect_exact("STOPPED\r\n", timeout=30)
        dut.write("start")
        dut.expect_exact(ready, timeout=30)
        device.write("direct_status")
        device.expect_exact(f"DIRECT_STATUS 15 status=1 requested={hold} actual=0 known=0 pointer=1\r\n", timeout=5)
        device.write("hold")
        device.expect_exact("HELD 1 busy=1\r\n", timeout=5)
        device.write("stop")
        device.expect_exact("STOPPED\r\n", timeout=30)
        device.write("direct_status")
        device.expect_exact(f"DIRECT_STATUS 16 status=2 requested={hold} actual=0 known=0 pointer=1\r\n", timeout=5)
        device.write("end_status")
        device.expect_exact("END_STATUS 16 error=0\r\n", timeout=5)

        dut.write("stop")
        dut.expect_exact("STOPPED\r\n", timeout=30)
        device.write("second")
        device.expect_exact("SECOND_REGISTERED\r\n", timeout=5)
        device.write("start")
        device.expect_exact("DEVICE_STARTED\r\n", timeout=30)
        dut.write("start 1")
        dut.expect_exact(ready, timeout=30)
        device.write("second_capacities")
        device.expect_exact(f"SECOND_CAPACITIES 64 0 512 {4 * mps}\r\n", timeout=5)
        for length in (1, mps, hold):
            device.write(f"expect {length} 17 0")
            device.expect_exact(f"ARMED {length}\r\n", timeout=5)
            dut.write(f"exchange {length}")
            dut.expect_exact(f"ROUNDTRIP_OK {length}\r\n", timeout=10)
            device.expect_exact(f"ECHOED {length}\r\n", timeout=10)

        for board in (dut, device):
            board.write("stop")
            board.expect_exact("STOPPED\r\n", timeout=30)
        device.write("second_callback")
        device.expect_exact("CALLBACK_CONFIGURED\r\n", timeout=5)
        device.write("start")
        device.expect_exact("DEVICE_STARTED\r\n", timeout=30)
        dut.write("start 1")
        dut.expect_exact(ready, timeout=30)
        device.write("second_capacities")
        device.expect_exact(f"SECOND_CAPACITIES 64 0 0 {4 * mps}\r\n", timeout=5)
        for length in (1, mps, hold):
            device.write(f"expect {length} {hold} 0")
            device.expect_exact(f"ARMED {length}\r\n", timeout=5)
            dut.write(f"exchange {length}")
            dut.expect_exact(f"ROUNDTRIP_OK {length}\r\n", timeout=10)
            device.expect_exact(f"ECHOED {length}\r\n", timeout=10)
        for board in (dut, device):
            board.write("stop")
            board.expect_exact("STOPPED\r\n", timeout=30)
        device.write("rx_mode")
        device.expect_exact("RX_MODE\r\n", timeout=5)
        device.write("start")
        device.expect_exact("DEVICE_STARTED\r\n", timeout=30)
        dut.write("start")
        dut.expect_exact(ready, timeout=30)
        device.write("capacities")
        device.expect_exact("CAPACITIES 0 0\r\n", timeout=5)
        device.write("rx_invalid")
        assert int(device.expect(r"RX_INVALID 1 max=(\d+)\r\n", timeout=5).group(1)) >= 131072
        count = 0
        for capacity, length, zlp in ((mps, 0, 1), (mps, 1, 0), (mps, mps - 1, 0), (mps, mps, 0),
                                      (2 * mps, mps + 1, 0), (2 * mps, 2 * mps - 1, 0), (2 * mps, 2 * mps, 0),
                                      (3 * mps, 2 * mps + 1, 0), (3 * mps, mps, 1), (3 * mps, 2 * mps, 1)):
            device.write(f"rx_arm {capacity} {length}")
            device.expect_exact("RX_ARMED 1 error=0\r\n", timeout=5)
            device.write("rx_busy")
            device.expect_exact("RX_BUSY 1\r\n", timeout=5)
            dut.write(f"send {length} {zlp}")
            dut.expect_exact(f"SENT {length} zlp={zlp} ok=1\r\n", timeout=10)
            count += 1
            device.write("rx_result")
            device.expect_exact(f"RX_RESULT {count} status=0 requested={capacity} actual={length} known=1 pointer=1 guards=1 payload=1\r\n", timeout=5)

        # A full packet alone does not finish a larger posted receive.
        device.write(f"rx_arm {3 * mps} {mps}")
        device.expect_exact("RX_ARMED 1 error=0\r\n", timeout=5)
        dut.write(f"send {mps} 0")
        dut.expect_exact(f"SENT {mps} zlp=0 ok=1\r\n", timeout=10)
        device.write("rx_count")
        device.expect_exact(f"RX_COUNT {count}\r\n", timeout=5)
        device.write("rx_busy")
        device.expect_exact("RX_BUSY 1\r\n", timeout=5)
        dut.write("send 0 1")
        dut.expect_exact("SENT 0 zlp=1 ok=1\r\n", timeout=10)
        count += 1
        device.write("rx_result")
        device.expect_exact(f"RX_RESULT {count} status=0 requested={3 * mps} actual={mps} known=1 pointer=1 guards=1 payload=1\r\n", timeout=5)

        device.write("rx_chain")
        device.expect_exact("RX_CHAIN_ARMED 1\r\n", timeout=5)
        for length in (1, mps - 1):
            dut.write(f"send {length} 0")
            dut.expect_exact(f"SENT {length} zlp=0 ok=1\r\n", timeout=10)
        device.write("rx_chain_result")
        device.expect_exact("RX_CHAIN_RESULT 1\r\n", timeout=5)
        count += 2

        for status in (1, 2):
            device.write(f"rx_arm {3 * mps} {mps}")
            device.expect_exact("RX_ARMED 1 error=0\r\n", timeout=5)
            dut.write(f"send {mps} 0")
            dut.expect_exact(f"SENT {mps} zlp=0 ok=1\r\n", timeout=10)
            if status == 1:
                dut.write("stop")
                dut.expect_exact("STOPPED\r\n", timeout=30)
                dut.write("start")
                dut.expect_exact(ready, timeout=30)
            else:
                device.write("stop")
                device.expect_exact("STOPPED\r\n", timeout=30)
            count += 1
            device.write("rx_result")
            device.expect_exact(f"RX_RESULT {count} status={status} requested={3 * mps} actual=0 known=0 pointer=1 guards=1 payload=1\r\n", timeout=5)
        dut.write("stop")
        dut.expect_exact("STOPPED\r\n", timeout=30)
        device.write("cycles")
        result = device.expect(r"CYCLES 1 before=(\d+) after=(\d+)\r\n", timeout=60)
        assert result.group(1) == result.group(2), "heap changed in borrowed-only mode"

        device.write("start")
        device.expect_exact("DEVICE_STARTED\r\n", timeout=30)
        dut.write("start")
        dut.expect_exact(ready, timeout=30)
        for cycle in range(20):
            device.write(f"rx_arm {3 * mps} {mps}")
            device.expect_exact("RX_ARMED 1 error=0\r\n", timeout=5)
            if cycle % 2:
                dut.write(f"send {mps} 0")
                dut.expect_exact(f"SENT {mps} zlp=0 ok=1\r\n", timeout=10)
            device.write("hold")
            device.expect_exact("HELD 1 busy=1\r\n", timeout=5)
            if cycle % 2:
                dut.write("prefix")
                dut.expect_exact("PREFIX_OK 1\r\n", timeout=10)
            device.write("cancel_both")
            device.expect_exact("CANCEL_REQUEST tx=0 rx=0\r\n", timeout=5)
            device.write(f"tx_wait {17 + 2 * cycle}")
            device.expect_exact(f"TX_WAIT {17 + 2 * cycle}\r\n", timeout=5)
            device.write("direct_status")
            device.expect_exact(f"DIRECT_STATUS {17 + 2 * cycle} status=4 requested={hold} actual=0 known=0 pointer=1\r\n", timeout=5)
            device.write("rx_result")
            device.expect_exact(f"RX_RESULT {16 + 2 * cycle} status=4 requested={3 * mps} actual=0 known=0 pointer=1 guards=1 payload=1\r\n", timeout=5)
            device.write("cancel_empty")
            device.expect_exact("CANCEL_EMPTY tx=259 rx=259\r\n", timeout=5)

            # Re-use each endpoint without reset/reconfiguration, preserving PID.
            device.write(f"rx_arm {mps} {mps - 1}")
            device.expect_exact("RX_ARMED 1 error=0\r\n", timeout=5)
            device.write("cancel_stale")
            device.expect_exact("CANCEL_STALE tx=259 rx=259\r\n", timeout=5)
            dut.write(f"send {mps - 1} 0")
            dut.expect_exact(f"SENT {mps - 1} zlp=0 ok=1\r\n", timeout=10)
            device.write("rx_result")
            device.expect_exact(f"RX_RESULT {17 + 2 * cycle} status=0 requested={mps} actual={mps - 1} known=1 pointer=1 guards=1 payload=1\r\n", timeout=5)
            device.write(f"tx_post {mps - 1}")
            device.expect_exact("TX_POSTED 1 error=0\r\n", timeout=5)
            device.write("cancel_stale")
            device.expect_exact("CANCEL_STALE tx=259 rx=259\r\n", timeout=5)
            dut.write(f"receive {mps - 1}")
            dut.expect_exact(f"READ_OK {mps - 1}\r\n", timeout=10)
            device.write(f"tx_wait {18 + 2 * cycle}")
            device.expect_exact(f"TX_WAIT {18 + 2 * cycle}\r\n", timeout=5)
            device.write("direct_status")
            device.expect_exact(f"DIRECT_STATUS {18 + 2 * cycle} status=0 requested={mps - 1} actual={mps - 1} known=1 pointer=1\r\n", timeout=5)
        for board in (dut, device):
            board.write("stop")
            board.expect_exact("STOPPED\r\n", timeout=30)
        device.write("copy_terminated")
        device.expect_exact("COPY_TERMINATED 1\r\n", timeout=5)
        device.write("start")
        device.expect_exact("DEVICE_STARTED\r\n", timeout=30)
        dut.write("start")
        dut.expect_exact(ready, timeout=30)
        for length in lengths:
            device.write(f"copy_post {length}")
            device.expect_exact(f"COPY_POSTED {length}\r\n", timeout=5)
            dut.write(f"receive {length} 1")
            dut.expect_exact(f"READ_OK {length}\r\n", timeout=10)
    finally:
        # Always attempt both stops, also after an assertion/timeout. No hook
        # changes report outcomes or starts a hidden recovery sequence.
        errors = []
        for name, board in (("host", dut), ("device", device)):
            try:
                board.write("stop")
                board.expect_exact("STOPPED\r\n", timeout=30)
            except Exception as exc:
                errors.append(f"{name}: {exc}")
        assert not errors, "USB stop failed: " + "; ".join(errors)
