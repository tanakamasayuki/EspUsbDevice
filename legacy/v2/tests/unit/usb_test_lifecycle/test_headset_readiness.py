"""Incomplete readiness replies must not prevent a fresh prerequisite query."""

import importlib.util
from pathlib import Path

import pexpect
import pytest


@pytest.fixture
def headset_test():
    path = Path(__file__).resolve().parents[2] / "peer/usb_audio_headset/test_usb_audio_headset.py"
    spec = importlib.util.spec_from_file_location("headset_readiness", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class Host:
    def __init__(self, replies):
        self.replies = iter(replies)
        self.commands = []

    def write(self, command):
        self.commands.append(command)

    def expect(self, pattern, timeout):
        assert pattern.endswith(r"\r?\n")
        assert timeout == 1
        reply = next(self.replies)
        if reply != "ready":
            raise pexpect.TIMEOUT(reply)


def test_queries_after_incomplete_and_unready_replies(headset_test, monkeypatch):
    ticks = iter([0, 0, 1, 2])
    monkeypatch.setattr(headset_test.time, "monotonic", lambda: next(ticks))
    host = Host(["HOST_AUDI", "HOST_AUDIO addr=1 out=0 in=0\n", "ready"])
    headset_test._ready_both_directions(host, None)
    assert host.commands == [b"i", b"i", b"i"]


def test_missing_readiness_fails_at_deadline(headset_test, monkeypatch):
    ticks = iter([0, 0, 20])
    monkeypatch.setattr(headset_test.time, "monotonic", lambda: next(ticks))
    host = Host(["HOST_AUDI"])
    with pytest.raises(AssertionError, match="not ready in both directions"):
        headset_test._ready_both_directions(host, None)
    assert host.commands == [b"i"]
