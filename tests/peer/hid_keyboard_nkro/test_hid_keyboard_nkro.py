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
  - state_report     10 usages plus Left Shift sent as ONE report via
                     sendReport(EspUsbDeviceNkroKeyboardReport) all arrive, and
                     heldState() mirrors what was sent. The modifier is checked
                     through the mod= field, not as an eleventh PRESS line:
                     EspUsbHost diffs bitmap bits into key events and carries
                     0xE0-0xE7 in event.modifiers instead.
  - nkro_disabled    the state overload refuses when enableNkro() was not called,
                     instead of silently dropping the seventh key onwards

One test rather than four: the first of the four read `HOST_CONNECTED`, a banner
the host prints once at connect, and the rest inherited the enumeration it had
waited for. Both sketches answer rather than announce now - see
tests/peer/composite_hid_cdc for the full note. The cases are named functions
driven from a list; each opens with a host-side reset that flushes stale key
events, so the order is not load-bearing.
"""

import re

# 'c' chord in peer_device: A S D F G H J K.
CHORD_C = {0x04, 0x16, 0x07, 0x09, 0x0a, 0x0b, 0x0d, 0x0e}
# 'j' chord: A, International1 (0x87), International3 (0x89), LANG1 (0x90), LANG2 (0x91).
CHORD_J = {0x04, 0x87, 0x89, 0x90, 0x91}

# 's' chord in peer_device: A S D F G H J K L ; plus Left Shift (0xe1).
CHORD_STATE = {0x04, 0x16, 0x07, 0x09, 0x0a, 0x0b, 0x0d, 0x0e, 0x0f, 0x33}
CHORD_STATE_MODIFIER = 0xE1

PRESS_RE = re.compile(r"PRESS keycode=0x([0-9a-fA-F]{2})")
PRESS_MOD_RE = re.compile(r"PRESS keycode=0x([0-9a-fA-F]{2}) n=\d+ mod=0x([0-9a-fA-F]{2})")
# Left Shift (usage 0xE1) is bit 1 of the modifier byte.
LEFT_SHIFT_BIT = 0x02
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


def _reset(dut):
    """Sync barrier: clears the host's counters and flushes stale PRESS/RELEASE
    lines, so what a case collects afterwards belongs to that case."""
    dut.write("r")
    dut.expect_exact("RESET")


def _exact_chord(dut, device):
    """Finer than a count check: every one of the eight held keys must arrive at
    the host with its exact keycode."""
    _reset(dut)

    device.write("c")
    device.expect(r"SENT_CHORD n=8 protocol=report")

    seen = _collect_presses(dut, len(CHORD_C))
    assert CHORD_C <= seen, (
        sorted(hex(k) for k in CHORD_C), sorted(hex(k) for k in seen)
    )

    dut.write("m")
    m = dut.expect(MAX_RE, timeout=10)
    assert int(m.group(1)) >= len(CHORD_C), _text(m.group(0))


def _high_usage_keys(dut, device):
    """Different angle: International / LANG (JIS) keys live at high usages
    (0x87-0x91), only reachable because the NKRO bitmap spans 0x00-0xDF. Each
    high keycode must arrive; a truncated bitmap would drop them silently."""
    _reset(dut)

    device.write("j")
    device.expect(re.compile(r"SENT_CHORD_JIS n=\d+"))

    seen = _collect_presses(dut, len(CHORD_J))
    assert CHORD_J <= seen, (
        sorted(hex(k) for k in CHORD_J), sorted(hex(k) for k in seen)
    )

    dut.write("m")
    m = dut.expect(MAX_RE, timeout=10)
    assert int(m.group(1)) >= len(CHORD_J), _text(m.group(0))


def _state_report(dut, device):
    """The whole held-key state in a single report.

    The incremental path spaces its presses out (see sendChord() in peer_device)
    because each pressUsage() emits its own report. sendReport() with the state
    type emits one, so all ten bitmap keys must reach the host from that single
    report, each carrying Left Shift - which press() routed into the modifier
    byte - in event.modifiers. heldState() is checked on the device side in the
    same breath, including that releaseAll() clears the modifier too.
    """
    _reset(dut)

    device.write("s")
    # held=10 covers the bitmap usages, mod=1 the Left Shift routed out of it.
    device.expect_exact("SENT_STATE ok=1 n=10 held=10 mod=1 protocol=report")

    # Every bitmap key of the chord, each carrying Left Shift in the modifier
    # byte because they all came out of the same report.
    seen = set()
    mods = set()
    for _ in range(len(CHORD_STATE)):
        m = dut.expect(PRESS_MOD_RE, timeout=10)
        seen.add(int(_text(m.group(1)), 16))
        mods.add(int(_text(m.group(2)), 16))
    assert CHORD_STATE <= seen, (
        sorted(hex(k) for k in CHORD_STATE), sorted(hex(k) for k in seen)
    )
    assert all(mod & LEFT_SHIFT_BIT for mod in mods), sorted(hex(m) for m in mods)

    # releaseAll() must clear both the bitmap and the modifier byte.
    device.expect_exact("RELEASED held_a=0 mod=0")

    dut.write("m")
    m = dut.expect(MAX_RE, timeout=10)
    assert int(m.group(1)) >= len(CHORD_STATE), _text(m.group(0))


def _state_report_requires_enable_nkro(dut, device):
    """Without enableNkro() the state overload must fail, not half-work.

    Folding an 11-key state down to six here would make the seventh key onwards
    vanish for good, and a sketch that forgot enableNkro() would never find out.
    (Boot protocol is the opposite case - the host chose it, so the state is
    folded down rather than refused.) The spare keyboard object this uses is not
    attached to a running device, so it leaves the one under test alone.
    """
    device.write("x")
    device.expect_exact("NKRO_DISABLED_SEND ok=0 nkro=0")


def test_hid_keyboard_nkro(dut, peers):
    device = peers["device"]

    device.write("?")
    device.expect_exact("DEVICE_READY 1")
    device.expect_exact("DEVICE_NKRO nkro=1")
    # The host end: it decoded the report descriptor as a bitmap report, which is
    # the premise of every case below.
    dut.write("i")
    dut.expect_exact("NKRO bitmap=1")

    checks = (
        _exact_chord,
        _high_usage_keys,
        _state_report,
        _state_report_requires_enable_nkro,
    )
    for check in checks:
        check(dut, device)
