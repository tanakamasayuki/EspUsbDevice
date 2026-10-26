"""A fresh S3 pair probe using only the plugin's dut/peers fixtures."""


def test_vendor_roundtrip(dut, peers):
    device = peers["device"]
    try:
        # Reconfigure the same binary, including small -> large -> small.
        for rx, tx, transfer, automatic in ((64, 64, 64, 1), (2048, 4096, 256, 1), (97, 131, 128, 1),
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
            dut.expect_exact("HOST_READY mps_in=64 mps_out=64\r\n", timeout=30)
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

            cases = [(n, 129, not automatic) for n in (1, 63, 64, 65, 127, 128, 129)]
            cases += [(n, chunk, True) for n in (64, 129) for chunk in (1, 17)]
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
        dut.expect_exact("HOST_READY mps_in=64 mps_out=64\r\n", timeout=30)
        device.write("direct_invalid")
        device.expect_exact("DIRECT_INVALID 1 max=65472\r\n", timeout=5)
        device.write("callback_mutation")
        device.expect_exact("CALLBACK_MUTATION_REJECTED 1\r\n", timeout=5)
        for length in (1, 63, 64, 65, 127, 128, 129):
            for terminate in (0, 1):
                device.write(f"expect {length} 129 0 {terminate}")
                device.expect_exact(f"ARMED {length}\r\n", timeout=5)
                dut.write(f"exchange {length} {terminate}")
                dut.expect_exact(f"ROUNDTRIP_OK {length}\r\n", timeout=10)
                device.expect_exact(f"ECHOED {length}\r\n", timeout=10)

        device.write("hold")
        device.expect_exact("HELD 1 busy=1\r\n", timeout=5)
        dut.write("stop")
        dut.expect_exact("STOPPED\r\n", timeout=30)
        dut.write("start")
        dut.expect_exact("HOST_READY mps_in=64 mps_out=64\r\n", timeout=30)
        device.write("direct_status")
        device.expect_exact("DIRECT_STATUS 15 status=1 requested=129 actual=0 known=0 pointer=1\r\n", timeout=5)
        device.write("hold")
        device.expect_exact("HELD 1 busy=1\r\n", timeout=5)
        device.write("stop")
        device.expect_exact("STOPPED\r\n", timeout=30)
        device.write("direct_status")
        device.expect_exact("DIRECT_STATUS 16 status=2 requested=129 actual=0 known=0 pointer=1\r\n", timeout=5)
        device.write("end_status")
        device.expect_exact("END_STATUS 16 error=0\r\n", timeout=5)

        dut.write("stop")
        dut.expect_exact("STOPPED\r\n", timeout=30)
        device.write("second")
        device.expect_exact("SECOND_REGISTERED\r\n", timeout=5)
        device.write("start")
        device.expect_exact("DEVICE_STARTED\r\n", timeout=30)
        dut.write("start 1")
        dut.expect_exact("HOST_READY mps_in=64 mps_out=64\r\n", timeout=30)
        device.write("second_capacities")
        device.expect_exact("SECOND_CAPACITIES 64 0 512 256\r\n", timeout=5)
        for length in (1, 64, 129):
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
        dut.expect_exact("HOST_READY mps_in=64 mps_out=64\r\n", timeout=30)
        device.write("second_capacities")
        device.expect_exact("SECOND_CAPACITIES 64 0 0 256\r\n", timeout=5)
        for length in (1, 64, 129):
            device.write(f"expect {length} 129 0")
            device.expect_exact(f"ARMED {length}\r\n", timeout=5)
            dut.write(f"exchange {length}")
            dut.expect_exact(f"ROUNDTRIP_OK {length}\r\n", timeout=10)
            device.expect_exact(f"ECHOED {length}\r\n", timeout=10)
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
