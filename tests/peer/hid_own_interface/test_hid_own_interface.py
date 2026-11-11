"""A Keyboard on an interface of its own beside HID Functions that share one."""

# layout: (LAYOUT reply, keyboard interface, report IDs per interface,
#          STARTED IDs, mouse interface, mouse report, consumer report)
LAYOUTS = {
    # Keyboard registered first but on its own interface, after the merged one.
    "merged": ("2 0:0/0 1:1/1", 1, {0: "2,4", 1: "none"},
               "keyboard_id=0 mouse_id=2 consumer_id=4", 0, "020105fb07", "04e900"),
    # A single shared Function keeps its own boot interface, in registration order.
    "single": ("2 0:1/2 1:1/1", 1, {0: "none", 1: "none"},
               "keyboard_id=0 mouse_id=0 consumer_id=0", 0, "0105fb07", None),
    # Without the option everything shares interface 0, as before.
    "shared": ("1 0:0/0", 0, {0: "2,4,1"},
               "keyboard_id=1 mouse_id=2 consumer_id=4", 0, "020105fb07", "04e900"),
}


def test_hid_own_interface(dut, peers):
    device = peers["device"]

    def command(board, text, reply, timeout=10):
        board.write(text)
        board.expect_exact(reply + "\r\n", timeout=timeout)

    try:
        for name, (layout, kbd, ids, started, mif, mouse, consumer) in LAYOUTS.items():
            own = name != "shared"
            command(device, f"start {name}", f"STARTED 1 {started}", 30)
            command(dut, "start", "HOST_READY 1", 30)
            command(dut, "layout", f"LAYOUT {layout}")
            for number, expected in ids.items():
                command(dut, f"report {number}", f"REPORT 1 ids={expected}")
            # Each interface carries only its own Functions' input reports.
            command(device, "key 4", "SENT 1")
            command(dut, f"read {kbd}", "READ 1 0000040000000000" if own else "READ 1 010000040000000000")
            command(device, "mouse 5", "SENT 1")
            command(dut, f"read {mif}", f"READ 1 {mouse}")
            if consumer:
                command(device, "consumer 233", "SENT 1")
                command(dut, "read 0", f"READ 1 {consumer}")
            # LED output reaches the Keyboard through its own interface.
            command(dut, f"led {kbd} {0 if own else 1} 3", "LED 1")
            command(device, "state", "STATE leds=3 protocol=1")
            if own:
                # Boot protocol is selected on the Keyboard's interface alone.
                command(dut, f"protocol {kbd} 0", "PROTOCOL 1")
                command(device, "state", "STATE leds=3 protocol=0")
                command(device, "key 6", "SENT 1")
                command(dut, f"read {kbd}", "READ 1 0000060000000000")
                command(dut, f"protocol {kbd} 1", "PROTOCOL 1")
                command(device, "state", "STATE leds=3 protocol=1")
            if name == "merged":
                # The merged interface is not a boot interface.
                command(dut, "protocol 0 0", "PROTOCOL 0")
                command(device, "mouse 5", "SENT 1")
                command(dut, "read 0", f"READ 1 {mouse}")
            for board in (dut, device):
                command(board, "stop", "STOPPED", 30)
    finally:
        errors = []
        for name, board in (("host", dut), ("device", device)):
            try:
                board.write("stop")
                board.expect_exact("STOPPED\r\n", timeout=30)
            except Exception as exc:
                errors.append(f"{name}: {exc}")
        assert not errors, "USB stop failed: " + "; ".join(errors)
