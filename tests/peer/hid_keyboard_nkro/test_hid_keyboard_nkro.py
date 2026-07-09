"""NKRO keyboard peer test, EspUsbDevice-repo copy.

DUT = the USB host (EspUsbHost, ``hid_keyboard_nkro.ino``); the peer = the
EspUsbDevice NKRO keyboard (``peer_device/``). Deliberately a different angle
from the EspUsbHost-repo copy (which checks bitmap detection + that 8 keys are
held at once, i.e. the count):

  - exact_chord      the host receives the EXACT set of 8 held keycodes, not
                     just that 8 keys were down (guards bitmap bit-position bugs)
  - high_usage_keys  a chord of International / LANG usages (0x87-0x91) all
                     arrive, proving the bitmap spans the full 0x00-0xDF range
                     that JIS / non-US layouts need (a 0x00-0x77 bitmap would
                     silently drop them)
"""

import re

# 'c' chord in peer_device: A S D F G H J K.
CHORD_C = {0x04, 0x16, 0x07, 0x09, 0x0a, 0x0b, 0x0d, 0x0e}
# 'j' chord: A, International1 (0x87), International3 (0x89), LANG1 (0x90), LANG2 (0x91).
CHORD_J = {0x04, 0x87, 0x89, 0x90, 0x91}

PRESS_RE = re.compile(r"PRESS keycode=0x([0-9a-fA-F]{2})")
MAX_RE = re.compile(r"MAX n=(\d+)")


def _text(group):
    # pytest-embedded matches a bytes buffer, so groups come back as bytes.
    return group.decode() if isinstance(group, bytes) else group


def _collect_presses(dut, count, timeout=10):
    """Read `count` PRESS events and return the set of keycodes seen. Order-
    independent so it does not matter whether the host coalesces reports."""
    seen = set()
    for _ in range(count):
        m = dut.expect(PRESS_RE, timeout=timeout)
        seen.add(int(_text(m.group(1)), 16))
    return seen


def _ready(device):
    device.write("?")
    device.expect(r"DEVICE_READY nkro=1")


def test_hid_keyboard_nkro_exact_chord(dut, peers):
    """Finer than a count check: every one of the eight held keys must arrive at
    the host with its exact keycode."""
    device = peers["device"]
    _ready(device)
    dut.expect_exact("HOST_CONNECTED")

    # Reset acts as a sync barrier: it flushes stale PRESS/RELEASE lines so the
    # PRESS events collected below belong to this chord.
    dut.write("r")
    dut.expect_exact("RESET")

    device.write("c")
    device.expect(r"SENT_CHORD n=8 protocol=report")

    seen = _collect_presses(dut, len(CHORD_C))
    assert CHORD_C <= seen, (
        sorted(hex(k) for k in CHORD_C), sorted(hex(k) for k in seen)
    )

    dut.write("m")
    m = dut.expect(MAX_RE, timeout=10)
    assert int(m.group(1)) >= len(CHORD_C), _text(m.group(0))


def test_hid_keyboard_nkro_high_usage_keys(dut, peers):
    """Different angle: International / LANG (JIS) keys live at high usages
    (0x87-0x91), only reachable because the NKRO bitmap spans 0x00-0xDF. Each
    high keycode must arrive; a truncated bitmap would drop them silently."""
    device = peers["device"]
    _ready(device)

    dut.write("r")
    dut.expect_exact("RESET")

    device.write("j")
    device.expect(re.compile(r"SENT_CHORD_JIS n=\d+"))

    seen = _collect_presses(dut, len(CHORD_J))
    assert CHORD_J <= seen, (
        sorted(hex(k) for k in CHORD_J), sorted(hex(k) for k in seen)
    )

    dut.write("m")
    m = dut.expect(MAX_RE, timeout=10)
    assert int(m.group(1)) >= len(CHORD_J), _text(m.group(0))
