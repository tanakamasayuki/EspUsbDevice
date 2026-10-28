"""DFU waits for configuration without retrying the transfers under test."""

import importlib.util
from pathlib import Path

import pexpect
import pytest


@pytest.fixture
def dfu_test():
    path = Path(__file__).resolve().parents[2] / "peer/usb_dfu/test_usb_dfu.py"
    spec = importlib.util.spec_from_file_location("dfu_readiness", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ReadinessDevice:
    def __init__(self, responses):
        self.responses = iter(responses)
        self.commands = []

    def write(self, command):
        self.commands.append(command)

    def expect_exact(self, pattern, timeout):
        assert pattern == "DEVICE_READY 1\n"
        assert timeout == 0.5
        if next(self.responses) == 0:
            raise pexpect.TIMEOUT("DEVICE_READY 0\n")


def test_queries_again_after_unconfigured_response(dfu_test, monkeypatch):
    ticks = iter([0, 0, 1])
    monkeypatch.setattr(dfu_test.time, "monotonic", lambda: next(ticks))
    device = ReadinessDevice([0, 1])
    dfu_test._wait_device_ready(device)
    assert device.commands == [b"?", b"?"]


def test_unconfigured_device_fails_at_deadline(dfu_test, monkeypatch):
    ticks = iter([0, 0, 20])
    monkeypatch.setattr(dfu_test.time, "monotonic", lambda: next(ticks))
    device = ReadinessDevice([0])
    with pytest.raises(AssertionError, match="not configured"):
        dfu_test._wait_device_ready(device)
    assert device.commands == [b"?"]
