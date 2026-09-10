"""Keyboard layout tables, one layout per case, across two boards.

One test rather than four. The first of the four read `DEVICE_BEGIN` and the
host's `HOST_CONNECTED` banner - both printed once at boot - and the other three
inherited the enumeration it had waited for. Run on its own, any of the other
three raced the enumeration.

Both sketches answer rather than announce now: the device blocks on
`device.ready()` and the host on its latched address, each before handling a
command. The host also gained a '?' query, because everything else it prints
arrives from a callback and a test needs some way to ask whether the peer is
attached.

The cases are named functions driven from a list. Each selects its own layout on
both sides before sending anything, so the order is not load-bearing.
"""

import time

import pexpect


def set_layout(dut, device, command, name):
    dut.write(command)
    dut.expect_exact(f"HOST_LAYOUT {name}")
    device.write(command)
    device.expect_exact(f"DEVICE_LAYOUT {name}")
    time.sleep(0.5)


def expect_key(dut, device, key):
    """Send `key` and wait for the host to decode it back to the same character.

    Retried: a HID report can be dropped while the host is still settling after
    a layout change, and a retry sends an identical report, so a retry that
    succeeds proves the same thing the first attempt would have.
    """
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


def _en_us(dut, device):
    set_layout(dut, device, "E", "EN_US")
    for key in ["@", "^"]:
        expect_key(dut, device, key)


def _ja_jp(dut, device):
    """Same two characters as en_US, on different keys: this is the check that
    the layout selection reaches the reverse lookup at all."""
    set_layout(dut, device, "J", "JA_JP")
    for key in ["@", "^", ":", "_"]:
        expect_key(dut, device, key)


def _de_de_altgr(dut, device):
    """de_DE AltGr layer: characters unreachable on base/Shift must be sent as
    the correct usage with the Right Alt (0x40) modifier."""
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


def _pt_br(dut, device):
    """pt_BR 0x90 tableSize fix: '/' lives on International1 (usage 0x87), which
    is only reachable when the reverse lookup scans the extended table. Without
    the fix '/' would fall back to AltGr+Q (usage 0x14, modifier 0x40)."""
    set_layout(dut, device, "B", "PT_BR")
    expect_key_full(dut, device, "/", 0x87, 0x00)


def test_hid_keyboard_layout(dut, peers):
    device = peers["device"]

    device.write("?")
    device.expect_exact("DEVICE_READY 1")
    # The host end of the same question. pid is asserted here because nothing
    # else in this module reports it - the rest of what the host prints comes
    # from the keyboard callback.
    dut.write("?")
    dut.expect_exact("HOST_READY 1 vid=303a pid=4009")

    for check in (_en_us, _ja_jp, _de_de_altgr, _pt_br):
        check(dut, device)
