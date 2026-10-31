"""Public API semantics on the wire: keys combine, mouse buttons survive moves,
HID packet sizes follow the speed, vendor requests sharing a library code reach
the sketch, end() is bounded, and TEST_MODE is accepted only at high speed."""

KEYS = [
    "0000040000000000",  # pressUsage(4)
    "0000040500000000",  # pressUsage(5): both held
    "0200040500000000",  # pressUsage(LeftShift): modifier bit
    "0300040506000000",  # pressUsage(6, LeftCtrl)
    "0300040600000000",  # releaseUsage(5): the rest stays packed
    "0100040600000000",  # releaseUsage(LeftShift)
    "0100040607000000",  # tapUsage(7): added...
    "0100040600000000",  # ...and only 7 removed
    "0300040600000000",  # pressKey('A'): Shift + 4, 4 already held
    "0100060000000000",  # releaseKey('A'): 4 and Shift released
    "0100060400000000",  # pressUsage 4, 5, 7, 8, 9: six keys held
    "0100060405000000",
    "0100060405070000",
    "0100060405070800",
    "0100060405070809",
    "0000000000000000",  # seventh key refused without a report; releaseAll()
]
MOUSE = ["01000000", "0105fd00", "00010100", "02000000", "00000000"]


def command(board, text, reply, timeout=15):
    board.write(text)
    return board.expect_exact(reply + "\r\n", timeout=timeout)


def test_api_semantics(dut, peers, request):
    device = peers["device"]
    high_speed = "_hs_" in request.node.name
    speed = "high" if high_speed else "full"
    try:
        command(device, "local", "LOCAL ok=1")

        command(device, "start keyboard", "STARTED ok=1", 30)
        command(dut, "start", "HOST_READY 1", 30)
        command(device, "packet", f"PACKET speed={speed} hid=8")
        command(dut, "arm", "ARMED")
        command(device, "keys", "KEYS ok=1 full=1 unknown=1", 30)
        command(dut, f"dump {len(KEYS)}", "REPORTS n=16 " + " ".join(KEYS))
        # bRequest 0x01/0x02 are the WebUSB / MS OS 2.0 codes; requests other
        # than their descriptor reads still belong to the sketch.
        command(dut, "request 40 01 0 0 0", "REQUEST ok=1 actual=0")
        command(dut, "request 40 02 0 7 0", "REQUEST ok=1 actual=0")
        command(device, "requests", "REQUESTS app=2")
        command(dut, "request c0 01 1 2 64", "REQUEST ok=1 actual=21")  # GET_URL: 3 + len("example.com/espusb")
        command(device, "block", "BLOCKING")
        command(dut, "bulk", "BULK ok=1")
        command(device, "end_blocked", "END entered=1 first=ESP_ERR_TIMEOUT second=ESP_OK started=0", 30)
        command(dut, "stop", "STOPPED heap=1", 30)

        command(device, "start mouse", "STARTED ok=1", 30)
        command(dut, "start", "HOST_READY 1", 30)
        command(dut, "arm", "ARMED")
        command(device, "mouse", "MOUSE ok=1 invalid=1")
        command(dut, f"dump {len(MOUSE)}", f"REPORTS n={len(MOUSE)} " + " ".join(MOUSE))
        command(dut, "stop", "STOPPED heap=1", 30)

        # inputReportSize 200: 64 at full speed (USB 2.0 5.7.3), 200 at high speed.
        packet = 200 if high_speed else 64
        command(device, "start custom", "STARTED ok=1", 30)
        command(dut, "start", "HOST_READY 1", 30)
        command(dut, "packet", f"DECLARED ok=1 packet={packet}")
        command(device, "packet", f"PACKET speed={speed} hid={packet}")

        # TEST_MODE (USB 2.0 9.4.9): selector 5 is a host test; full speed stalls.
        command(dut, "request 00 03 2 0500 0", "REQUEST ok=0 actual=0")
        command(dut, "request 00 03 2 0300 0", f"REQUEST ok={int(high_speed)} actual=0")
        command(device, "test_mode", f"TEST_MODE selector={3 if high_speed else 0}")
        command(dut, "stop", "STOPPED heap=1", 30)
        # Only a reset leaves test mode.
        command(device, "restart", "RESTARTING")
    finally:
        dut.write("stop")
        device.write("stop")
