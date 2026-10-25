"""Exercise startup ordering and failure cleanup without acquiring a board."""

from pathlib import Path
import subprocess

import pexpect
import pytest

from usb_test_lifecycle import usb_test_session


class FakeDut:
    def __init__(self, name, events, stop_error=False, readiness_timeouts=0):
        self.name = name
        self.events = events
        self.stop_error = stop_error
        self.readiness_timeouts = readiness_timeouts

    def write(self, command):
        self.events.append((self.name, "write", command))

    def expect(self, pattern, timeout):
        self.events.append((self.name, "ready", pattern))
        if self.readiness_timeouts:
            self.readiness_timeouts -= 1
            raise pexpect.TIMEOUT("still booting")

    def expect_exact(self, pattern, timeout):
        self.events.append((self.name, "stop", pattern))
        if self.stop_error:
            raise pexpect.TIMEOUT("shutdown hung")


def test_peer_setup_finishes_before_host_starts():
    events = []
    host = FakeDut("host", events, readiness_timeouts=1)
    peer = FakeDut("peer", events, readiness_timeouts=1)
    with usb_test_session(host, "example", [peer]):
        events.append(("test", "body", "assertions"))

    assert [e for e in events if e[1] == "write"] == [
        ("host", "write", b"Q"), ("host", "write", b"Q"),
        ("peer", "write", b"\x1c"), ("peer", "write", b"\x1c"),
        ("host", "write", b"G"),
        ("host", "write", b"\x1f"), ("peer", "write", b"\x1f"),
    ]
    start = events.index(("host", "write", b"G"))
    # The body reads enumeration itself; startup must not consume its output.
    assert events[start + 1] == ("test", "body", "assertions")


def test_failed_assertion_still_stops_every_board():
    events = []
    host = FakeDut("host", events, stop_error=True)
    peers = [FakeDut("peer1", events, stop_error=True), FakeDut("peer2", events)]
    with pytest.warns(UserWarning, match="USB shutdown was not acknowledged"):
        with pytest.raises(AssertionError, match="product failure"):
            with usb_test_session(host, "example", peers):
                raise AssertionError("product failure")
    assert [e[0] for e in events if e[1] == "stop"] == ["host", "peer1", "peer2"]


def test_readiness_failure_never_starts_host_and_still_cleans_up():
    events = []
    host = FakeDut("host", events)
    peer = FakeDut("peer", events, readiness_timeouts=30)
    with pytest.raises(AssertionError, match="TEST_PEER_BOOTED"):
        with usb_test_session(host, "example", [peer]):
            pytest.fail("test body ran without a ready peer")
    assert ("host", "write", b"G") not in events
    assert [e[0] for e in events if e[1] == "stop"] == ["host", "peer"]


def test_sketch_gate_and_shutdown(tmp_path):
    root = Path(__file__).resolve().parents[3]
    (tmp_path / "Arduino.h").write_text(
        """#pragma once
#include <cstdio>
#include <cstdarg>
#include <deque>
#include <string>
struct FakeSerial {
  std::deque<int> input;
  std::string output;
  int available() { return input.size(); }
  int peek() { return input.empty() ? -1 : input.front(); }
  int read() { int c = peek(); input.pop_front(); return c; }
  void println(const char *s) { output += s; output += "\\r\\n"; }
  void printf(const char *format, ...) {
    char buffer[256]; va_list args; va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args); va_end(args);
    output += buffer;
  }
};
inline FakeSerial Serial;
inline void delay(int) {}
"""
    )
    source = tmp_path / "lifecycle.cpp"
    source.write_text(
        """#include "UsbTestLifecycle.h"
#include <cassert>
int main() {
  Serial.input = {'Q', 'Q', 'G'};
  waitForUsbTestStart("example");
  assert(Serial.output == "TEST_IDLE example\\nTEST_IDLE example\\n");
  int ends = 0;
  auto stop = [&] { ++ends; };
  Serial.input = {'h'};
  assert(!usbTestControlPending());
  assert(!handleUsbTestStop(stop, "example"));
  assert(Serial.peek() == 'h' && ends == 0);
  Serial.input = {0x1c};
  assert(usbTestControlPending());
  assert(handleUsbTestStop(stop, "example"));
  assert(Serial.output.find("TEST_PEER_BOOTED example\\n") != std::string::npos);
  assert(ends == 0);
  Serial.input = {'h', 0x1f};
  std::string applicationInput;
  while (Serial.available() > 0 && !usbTestControlPending()) {
    applicationInput += static_cast<char>(Serial.read());
  }
  assert(applicationInput == "h" && Serial.peek() == 0x1f);
  assert(usbTestControlPending());
  assert(handleUsbTestStop(stop, "example"));
  assert(ends == 1);
  assert(Serial.output.find("TEST_STOPPING\\n") != std::string::npos);
  assert(handleUsbTestStop(stop, "example"));
  Serial.input = {0x1f};
  assert(handleUsbTestStop(stop, "example"));
  assert(ends == 1);
  assert(Serial.output.find("TEST_STOPPED\\r\\n") != std::string::npos);
}
"""
    )
    binary = tmp_path / "lifecycle"
    subprocess.run([
        "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-I", str(tmp_path), "-I", str(root / "tests/sketch_support"),
        str(source), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
