"""Synchronize firmware startup and leave both USB boards stopped on failure."""

from contextlib import contextmanager
import re

import pexpect


def _wait_ready(dut, command, response, identity):
    # Only readiness is retried; assertions in the test body are never retried.
    for _ in range(30):
        # SerialDut appends a newline to str writes. Send bytes so setup's G
        # cannot leave a newline ahead of the shutdown command in loopback.
        dut.write(command.encode())
        try:
            dut.expect(re.escape(f"{response} {identity}") + r"\r?\n", timeout=1)
            return
        except pexpect.TIMEOUT:
            continue
    raise AssertionError(f"{identity}: firmware did not report {response}")


def _stop(dut):
    dut.write(b"\x1f")
    dut.expect_exact("TEST_STOPPED\r\n", timeout=30)


@contextmanager
def usb_test_session(dut, identity, peers=()):
    peers = tuple(peers)
    try:
        _wait_ready(dut, "Q", "TEST_IDLE", identity)
        for peer in peers:
            _wait_ready(peer, "\x1c", "TEST_PEER_BOOTED", identity)
        dut.write(b"G")
        # Do not expect a start ACK: that would discard connect-time output.
        yield
    finally:
        # Attempt every board even if one hangs. The fixture's teardown error
        # must stop the matrix: the next case cannot assume the bus is idle.
        errors = []
        for label, target in [("host", dut), *[("peer", p) for p in peers]]:
            try:
                _stop(target)
            except Exception as exc:
                errors.append(f"{label} USB shutdown was not acknowledged: {exc}")
        if errors:
            raise RuntimeError(f"{identity}: " + "; ".join(errors))
