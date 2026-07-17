import time

import pexpect


def set_layout(dut, device, command, name):
    dut.write(command)
    dut.expect_exact(f"HOST_LAYOUT {name}")
    device.write(command)
    device.expect_exact(f"DEVICE_LAYOUT {name}")
    time.sleep(0.5)


def expect_key(dut, device, key):
    last_error = None
    for _ in range(3):
        device.write(key)
        device.expect_exact(f"SEND {key} 1")
        try:
            dut.expect_exact(f"KEY {key}", timeout=5)
            return
        except pexpect.TIMEOUT as err:
            last_error = err
    raise last_error


def expect_key_full(dut, device, key, keycode, modifiers):
    """Assert not just that `key` arrives, but with the exact usage keycode and
    modifier byte the device emitted - so AltGr (Right Alt) and the pt_BR
    International1 usage are actually verified, not merely the resulting glyph."""
    expected = f"KEY {key} keycode=0x{keycode:02x} modifiers=0x{modifiers:02x}"
    last_error = None
    for _ in range(3):
        device.write(key)
        device.expect_exact(f"SEND {key} 1")
        try:
            dut.expect_exact(expected, timeout=5)
            return
        except pexpect.TIMEOUT as err:
            last_error = err
    raise last_error


def test_hid_keyboard_layout_en_us(dut, peers):
    device = peers["device"]
    device.expect_exact("DEVICE_BEGIN 1")
    dut.expect_exact("HOST_CONNECTED vid=303a pid=4009")
    device.write("?")
    device.expect_exact("DEVICE_READY")
    time.sleep(1.0)

    set_layout(dut, device, "E", "EN_US")
    for key in ["@", "^"]:
        expect_key(dut, device, key)


def test_hid_keyboard_layout_ja_jp(dut, peers):
    device = peers["device"]
    device.write("?")
    device.expect_exact("DEVICE_READY")

    set_layout(dut, device, "J", "JA_JP")
    for key in ["@", "^", ":", "_"]:
        expect_key(dut, device, key)


def test_hid_keyboard_layout_de_de_altgr(dut, peers):
    """de_DE AltGr layer: characters unreachable on base/Shift must be sent as
    the correct usage with the Right Alt (0x40) modifier."""
    device = peers["device"]
    device.write("?")
    device.expect_exact("DEVICE_READY")

    set_layout(dut, device, "D", "DE_DE")
    ALT_GR = 0x40
    LEFT_SHIFT = 0x02
    # AltGr layer (KBDGR / DIN 2137 T1): char -> (usage keycode, modifier byte).
    for key, keycode in [
        ("@", 0x14),  # AltGr+Q
        ("{", 0x24),  # AltGr+7
        ("[", 0x25),  # AltGr+8
        ("|", 0x64),  # AltGr+<
    ]:
        expect_key_full(dut, device, key, keycode, ALT_GR)
    # Base/Shift path is unaffected by the AltGr fallback: '/' is Shift+7, i.e.
    # the same usage (0x24) as AltGr+7 '{' but a different modifier.
    expect_key_full(dut, device, "/", 0x24, LEFT_SHIFT)


def test_hid_keyboard_layout_pt_br(dut, peers):
    """pt_BR 0x90 tableSize fix: '/' lives on International1 (usage 0x87), which
    is only reachable when the reverse lookup scans the extended table. Without
    the fix '/' would fall back to AltGr+Q (usage 0x14, modifier 0x40)."""
    device = peers["device"]
    device.write("?")
    device.expect_exact("DEVICE_READY")

    set_layout(dut, device, "B", "PT_BR")
    expect_key_full(dut, device, "/", 0x87, 0x00)
