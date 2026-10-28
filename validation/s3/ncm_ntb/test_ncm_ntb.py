"""Raw NTB validation, independent NCM interfaces and alternate-setting recovery."""


def test_ncm_ntb(dut, peers):
    device = peers["device"]
    try:
        device.write("start")
        device.expect_exact("STARTED 1\r\n", timeout=30)
        dut.write("start")
        dut.expect_exact("HOST_READY 1\r\n", timeout=30)
        for command in ("matrix", "multi", "toggle"):
            dut.write(command)
            dut.expect_exact(command.upper() + " 1\r\n", timeout=60)
        device.write("stats")
        device.expect_exact("STATS 0 tx=43 rx=43 invalid=22\r\n", timeout=5)
        device.expect_exact("STATS 1 tx=1 rx=1 invalid=0\r\n", timeout=5)
        device.expect_exact("REJECTED 0\r\n", timeout=5)
        device.write("ip")
        device.expect_exact("IP_PAIR 1\r\n", timeout=20)
    finally:
        errors = []
        for name, board in (("host", dut), ("device", device)):
            try:
                board.write("stop")
                board.expect_exact("STOPPED\r\n", timeout=30)
            except Exception as exc:
                errors.append(f"{name}: {exc}")
        assert not errors, "USB stop failed: " + "; ".join(errors)


def test_ncm_halt(dut, peers):
    device = peers["device"]

    def command(board, text, reply, timeout=10):
        board.write(text)
        board.expect_exact(reply + "\r\n", timeout=timeout)

    def prepare(i):
        command(dut, f"select {i}", "SELECTED 1")
        command(dut, "prepare", "PREPARED 1")
        command(device, f"ready {i}", "READY 1")

    try:
        command(device, "start", "STARTED 1", 30)
        command(dut, "start", "HOST_READY 1", 30)
        for i in (0, 1):
            for endpoint in (0, 1):
                prepare(i)
                # Leave the connection notification pending at DATA1 while
                # the selected bulk pipe is halted and recovered.
                command(dut, "speed", "SPEED 1")
                command(device, f"armed {i} 2", "ARMED 1")
                command(dut, f"set {endpoint}", "FEATURE 1")
                command(device, f"ready {i}", "READY 1")
                command(dut, f"stalled {endpoint}", "STALLED 1")
                command(dut, "connection", "CONNECTION 1")
                # A different NCM Function remains usable while this is halted.
                command(dut, f"select {1-i}", "SELECTED 1")
                command(dut, "marker 115", "MARKER 1")
                command(dut, f"select {i}", "SELECTED 1")
                command(dut, f"status {endpoint} 1", "ENDPOINT_STATUS 1")
                if endpoint == 1:
                    command(device, f"blocked {i}", "SENT 1")
                else:
                    command(device, f"send {i} 114", "SENT 1")
                    command(dut, "receive 114", "RECEIVED 1")
                command(dut, f"clear {endpoint}", "FEATURE 1")
                command(device, f"ready {i}", "READY 1")
                command(dut, "marker 112", "MARKER 1")
            # OUT recovery discards its partial NTB, preserving unread IN.
            prepare(i)
            command(dut, "queued", "QUEUED 1")
            command(device, f"armed {i} 1", "ARMED 1")
            command(dut, "partial", "FRAGMENT 1")
            command(dut, "set 0", "FEATURE 1")
            command(dut, "clear 0", "FEATURE 1")
            command(device, f"ready {i}", "READY 1")
            command(dut, "receive 64", "RECEIVED 1")
            command(dut, "marker 112", "MARKER 1")
            # IN recovery discards old TX but preserves partial RX. Finish it
            # with a short packet; its tag differs from the abandoned echo.
            # The initial single OUT packet leaves that unaffected pipe at DATA1.
            for setting in (True, False):
                prepare(i)
                command(dut, "queued", "QUEUED 1")
                command(device, f"armed {i} 1", "ARMED 1")
                command(dut, "partial_one", "FRAGMENT 1")
                if setting:
                    command(dut, "set 1", "FEATURE 1")
                command(dut, "clear 1", "FEATURE 1")
                command(device, f"ready {i}", "READY 1")
                command(dut, "finish_one", "FRAGMENT 1")
                command(dut, "receive 85", "RECEIVED 1")
                command(dut, "marker 112", "MARKER 1")
            # Notification HALT/CLEAR preserves both bulk directions.
            prepare(i)
            command(dut, "queued", "QUEUED 1")
            command(device, f"armed {i} 1", "ARMED 1")
            command(dut, "set 2", "FEATURE 1")
            command(dut, "stalled 2", "STALLED 1")
            command(device, f"ready {i}", "READY 1")
            command(dut, "receive 64", "RECEIVED 1")
            command(dut, "marker 112", "MARKER 1")
            command(dut, "clear 2", "FEATURE 1")
            command(device, f"ready {i}", "READY 1")
            command(dut, "notifications", "NOTIFICATIONS 1")
            prepare(i)
            command(dut, "set 0", "FEATURE 1")
            command(dut, "set 1", "FEATURE 1")
            command(dut, "clear 1", "FEATURE 1")
            command(device, f"ready {i}", "READY 1")
            command(dut, "status 0 1", "ENDPOINT_STATUS 1")
            command(dut, "stalled 0", "STALLED 1")
            command(dut, "clear 0", "FEATURE 1")
            command(device, f"ready {i}", "READY 1")
            command(dut, "marker 113", "MARKER 1")
            for endpoint in (0, 1, 2):
                command(dut, f"clear {endpoint}", "FEATURE 1")
                command(device, f"ready {i}", "READY 1")
                command(dut, "marker 114", "MARKER 1")
                if endpoint == 2:
                    command(dut, "notifications", "NOTIFICATIONS 1")
        command(device, "ip", "IP_PAIR 1", 20)
    finally:
        errors = []
        for name, board in (("host", dut), ("device", device)):
            try:
                command(board, "stop", "STOPPED", 30)
            except Exception as exc:
                errors.append(f"{name}: {exc}")
        assert not errors, "USB stop failed: " + "; ".join(errors)
