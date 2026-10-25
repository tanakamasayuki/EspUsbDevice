"""A fresh S3 pair probe using only the plugin's dut/peers fixtures."""


def test_vendor_roundtrip(dut, peers):
    device = peers["device"]
    try:
        # Neither sketch starts USB during upload. These are normal sketch
        # commands, visible here rather than injected by an autouse fixture.
        device.write("start")
        device.expect_exact("DEVICE_STARTED\r\n", timeout=30)
        dut.write("start")
        dut.expect_exact("HOST_READY mps_in=64 mps_out=64\r\n", timeout=30)
        device.write("status")
        device.expect_exact("DEVICE_READY 1\r\n", timeout=10)

        cases = [(n, 129, False) for n in (1, 63, 64, 65, 127, 128, 129)]
        cases += [(n, chunk, flush) for n in (1, 64, 129)
                  for chunk in (1, 17) for flush in (False, True)]
        for length, chunk, flush in cases:
            device.write(f"expect {length} {chunk} {int(flush)}")
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
