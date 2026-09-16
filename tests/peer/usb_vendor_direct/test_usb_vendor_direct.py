"""The direct transfer path, end to end against a real USB host.

DUT = the USB host (EspUsbHost); the peer = an EspUsbDevice vendor device built
non-buffered (``CFG_TUD_VENDOR_TXRX_BUFFERED=0``), streaming 21-byte blocks with
``writeDirect()`` and arming the next one from inside ``onTxComplete()``.

``direct=1`` in the first line is ``directWriteSupported()``, i.e. the library's
own view of the build. Asserting it rather than the sketch's macro is what makes
this test notice a stale build: ``build_opt.h`` reaches the sketch through a
response file, so without ``--clean`` the sketch is rebuilt with the flag while
the library keeps its previous objects.

What this proves is not throughput - the rig is full speed, and the numbers come
from the ESP32-P4 - but that the path works at all: the endpoint claim succeeds,
the caller's own buffer reaches the host intact and in order, and the completion
callback arrives in time to keep the stream armed.

The host returns an arbitrary 63-byte window of one continuous byte stream: its
pipe keeps reading in the background, so a read starts and ends wherever that
buffer happens to sit, not on a block boundary. So the assertion is not about
how many blocks a read contains - it is that every complete block inside the
window is exactly the block the device says it sent, and that consecutive blocks
are consecutive. That holds whatever the window alignment, and it is what would
break if a transfer were torn, dropped or reordered.
"""

import re

BLOCK = 21
STAMP = re.compile(r"D(\d{4}):")


def _expected(seq):
    """The block the device sends for this sequence number, in full."""
    body = "".join(chr(ord("a") + ((i + seq) % 26)) for i in range(6, BLOCK))
    return f"D{seq % 10000:04d}:{body}"


def _complete_blocks(window):
    """Every whole block inside one read, verified byte for byte."""
    if isinstance(window, bytes):
        window = window.decode("ascii", "replace")
    found = []
    for m in STAMP.finditer(window):
        chunk = window[m.start():m.start() + BLOCK]
        if len(chunk) < BLOCK:
            continue  # the window ends mid-block; not this read's to judge
        seq = int(m.group(1))
        assert chunk == _expected(seq), f"block {seq} arrived as {chunk!r}"
        found.append(seq)
    return found


def test_usb_vendor_direct(dut, peers):
    device = peers["device"]

    # The device starts streaming once the host has enumerated it and announces
    # that once, at mount - which can happen before this log is open, since the
    # host may already be running when the device boots. So ask instead, and
    # keep asking: mounted=1 is the proof that enumeration happened, direct=1
    # that the library (not just the sketch) was built non-buffered, started=1
    # that the first block was armed.
    for attempt in range(20):
        device.write("?")
        m = device.expect(r"DEVICE_DIRECT_STATE mounted=(\d) direct=(\d) started=(\d)", timeout=5)
        if m.group(1) == b"1" and m.group(3) == b"1":
            break
    assert m.group(1) == b"1", "the device never mounted"
    assert m.group(2) == b"1", "the library was built buffered (stale build: needs --clean)"
    assert m.group(3) == b"1", "the device mounted but never armed its first block"

    dut.write("o")
    dut.expect_exact("VENDOR_OPEN 1")

    seen = []
    for _ in range(5):
        dut.write("r")
        # The window starts wherever the host's pipe sits, so do not anchor on a
        # stamp: capture the field and let _complete_blocks() find the blocks.
        m = dut.expect(r"VENDOR_READ len=\d+ data=(\S+)", timeout=10)
        stamps = _complete_blocks(m.group(1))
        assert stamps, f"no whole block in the read: {m.group(1)}"
        # Consecutive inside one window: a gap would mean a transfer was lost
        # between two the host did receive.
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
    assert byts == blocks * 21, f"bytes {byts} != blocks {blocks} * 21"
    assert armfail == 0, f"writeDirect() refused {armfail} time(s)"
    # Non-buffered has no ZLP logic of its own, so nothing should ever complete
    # with zero bytes. A non-zero count here is the buffered-mode ZLP leaking in.
    assert zerolen == 0, f"{zerolen} zero-length completion(s)"
