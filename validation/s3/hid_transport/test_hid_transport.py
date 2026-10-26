"""Descriptor-sized HID buffers, multi-packet reports, control and boot mode."""


def test_hid_transport(dut, peers):
    device = peers["device"]
    try:
        device.write("metadata")
        result = device.expect(r"METADATA ok=1 before=(\d+) after=(\d+)\r\n", timeout=30)
        assert result.group(1) == result.group(2), "HID descriptor capacities leaked heap"
        for length in (1, 63, 64, 65, 127, 128, 129, 255, 256, 257, 511, 1023):
            device.write(f"start {length}")
            device.expect_exact(f"STARTED 1 in={length + 1} out={length + 1} feature={length + 1}\r\n", timeout=30)
            dut.write(f"start {length}")
            dut.expect_exact("HOST_READY 1\r\n", timeout=30)
            dut.write(f"arm {length + 1}")
            dut.expect_exact("ARMED\r\n", timeout=5)
            device.write("send")
            device.expect_exact("SENT 1 rejected=1\r\n", timeout=5)
            dut.write("input")
            dut.expect_exact(f"INPUT 1 bytes={length + 1}\r\n", timeout=10)
            dut.write("output")
            dut.expect_exact("OUTPUT_SENT 1\r\n", timeout=15)
            device.write("result")
            device.expect_exact("OUTPUTS 3 features=1 payload=1\r\n", timeout=10)
            if length == 1023:
                device.write("drop")
                device.expect_exact("DROPPED 1\r\n", timeout=30)
            for board in (dut, device):
                board.write("stop")
                board.expect_exact("STOPPED\r\n", timeout=30)
        device.write("keyboard")
        device.expect_exact("KEYBOARD_STARTED 1\r\n", timeout=30)
        dut.write("keyboard")
        dut.expect_exact("HOST_READY 1\r\n", timeout=30)
        for protocol, length in ((0, 8), (1, 29)):
            dut.write(f"protocol {protocol}")
            dut.expect_exact(f"PROTOCOL_SET 1 value={protocol}\r\n", timeout=10)
            device.write("protocol")
            device.expect_exact(f"PROTOCOL {protocol} leds=5\r\n", timeout=5)
            dut.write(f"arm {length}")
            dut.expect_exact("ARMED\r\n", timeout=5)
            device.write("key")
            device.expect_exact("KEY_SENT 1\r\n", timeout=5)
            dut.write("input")
            dut.expect_exact(f"INPUT 1 bytes={length}\r\n", timeout=10)
        device.write("drop_keyboard")
        device.expect_exact("KEYBOARD_DROPPED 1 callbacks=1\r\n", timeout=30)
        dut.write("stop")
        dut.expect_exact("STOPPED\r\n", timeout=30)
        device.write("mouse")
        device.expect_exact("MOUSE_STARTED 1\r\n", timeout=30)
        dut.write("mouse")
        dut.expect_exact("HOST_READY 1\r\n", timeout=30)
        for protocol, length in ((0, 3), (1, 4)):
            dut.write(f"protocol {protocol}")
            dut.expect_exact(f"PROTOCOL_SET 1 value={protocol}\r\n", timeout=10)
            dut.write(f"arm {length}")
            dut.expect_exact("ARMED\r\n", timeout=5)
            device.write("move")
            device.expect_exact("MOVE_SENT 1\r\n", timeout=5)
            dut.write("input")
            dut.expect_exact(f"INPUT 1 bytes={length}\r\n", timeout=10)
            dut.write("mouse_idle")
            dut.expect_exact("MOUSE_IDLE 1\r\n", timeout=5)
    finally:
        errors = []
        for name, board in (("host", dut), ("device", device)):
            try:
                board.write("stop")
                board.expect_exact("STOPPED\r\n", timeout=30)
            except Exception as exc:
                errors.append(f"{name}: {exc}")
        assert not errors, "USB stop failed: " + "; ".join(errors)
