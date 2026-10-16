"""Compile the shipped HID sender against a controlled USB endpoint and clock."""

import subprocess
from pathlib import Path


def test_hid_send(tmp_path):
    source = (Path(__file__).resolve().parents[3] / "src/EspUsbDevice.cpp").read_text()
    start = source.index("bool EspUsbDevice::sendHidReport(")
    end = source.index("\nconst uint8_t *EspUsbDevice::deviceDescriptor()", start)
    sender = source[start:end]
    harness = Path(__file__).with_name("hid_send_test.cpp").read_text()
    generated = tmp_path / "hid_send.cpp"
    generated.write_text(harness.replace("// SHIPPED_SENDER", sender))
    binary = tmp_path / "hid_send"
    subprocess.run(
        ["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", str(generated), "-o", str(binary)],
        check=True, capture_output=True, text=True,
    )
    subprocess.run([str(binary)], check=True, capture_output=True, text=True)
