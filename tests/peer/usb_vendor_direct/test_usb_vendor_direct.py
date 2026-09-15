"""TEMPORARY - prototype coverage for CR-10 / F1. Delete with the prototype.

DUT = the USB host (EspUsbHost); the peer = an EspUsbDevice vendor device built
non-buffered (``CFG_TUD_VENDOR_TXRX_BUFFERED=0``), streaming 32-byte blocks with
``writeDirect()`` and arming the next one from inside ``onTxComplete()``.

What this proves is not throughput - the rig is full speed, and the numbers come
from the ESP32-P4 - but that the path works at all: the endpoint claim succeeds,
the caller's own buffer reaches the host intact and in order, and the completion
callback arrives in time to keep the stream armed.

The host coalesces: one read returns whatever its pipe holds, up to 63 bytes, so
a single read routinely carries two whole blocks. That is the host's behaviour,
not the device's, which is why the assertions are about block framing and
ordering rather than read length.
"""

import re

STAMP = re.compile(r"D(\d{4}):")


def _blocks_in(line):
    # pexpect hands back bytes; the stamps are ASCII either way.
    if isinstance(line, bytes):
        line = line.decode("ascii", "replace")
    return [int(m) for m in STAMP.findall(line)]


def test_usb_vendor_direct(dut, peers):
    device = peers["device"]

    # The device only starts streaming once the host has enumerated it, so this
    # line is also the proof that enumeration happened.
    device.expect("DEVICE_DIRECT_START ok=1 buffered=0", timeout=30)

    dut.write("o")
    dut.expect_exact("VENDOR_OPEN 1")

    seen = []
    for _ in range(5):
        dut.write("r")
        m = dut.expect(r"VENDOR_READ len=\d+ data=(D\d{4}:\S*)", timeout=10)
        stamps = _blocks_in(m.group(1))
        assert stamps, f"no whole block in the read: {m.group(1)}"
        # Blocks inside one read must be consecutive: a gap would mean a
        # transfer was lost between two the host did receive.
        for a, b in zip(stamps, stamps[1:]):
            assert b == (a + 1) % 10000, f"gap inside one read: {stamps}"
        seen.extend(stamps)

    # The stream has to advance. The same block five times would mean the
    # completion callback never re-armed.
    assert len(set(seen)) > 1, f"stream did not advance: {seen}"

    device.write("s")
    m = device.expect(r"DEVICE_DIRECT_STAT blocks=(\d+) bytes=(\d+) armfail=(\d+) zerolen=(\d+)")
    blocks, byts, armfail, zerolen = (int(m.group(i)) for i in range(1, 5))
    assert blocks > 0, "the completion callback never fired"
    assert byts == blocks * 32, f"bytes {byts} != blocks {blocks} * 32"
    assert armfail == 0, f"writeDirect() refused {armfail} time(s)"
    # Non-buffered has no ZLP logic of its own, so nothing should ever complete
    # with zero bytes. A non-zero count here is the buffered-mode ZLP leaking in.
    assert zerolen == 0, f"{zerolen} zero-length completion(s)"
