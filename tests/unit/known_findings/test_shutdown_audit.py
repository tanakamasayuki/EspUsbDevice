"""A disconnect allowance must not hide upload or application failures."""

import importlib.util
from pathlib import Path
import sys

import pytest


@pytest.fixture(scope="module")
def audit_module():
    path = Path(__file__).resolve().parents[2] / "conftest.py"
    spec = importlib.util.spec_from_file_location("espusb_serial_audit", path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


ERROR = "E (10) USB HOST: Enqueue URB error: ESP_ERR_INVALID_STATE\n"


@pytest.mark.parametrize("text,unexpected_count,known_count,group", [
    (ERROR, 1, 0, "loopback"),
    ("TEST_STOPPING\n" + ERROR + "TEST_STOPPED\n", 0, 1, "loopback"),
    ("TEST_STOPPING\n" + ERROR * 2 + "TEST_STOPPED\n", 1, 1, "loopback"),
    ("TEST_STOPPING\n" + ERROR, 1, 0, "loopback"),
    ("TEST_STOPPED\nTEST_STOPPING\n" + ERROR, 1, 0, "loopback"),
    (ERROR + "TEST_STOPPING\n" + ERROR + "TEST_STOPPED\n" + ERROR, 2, 1, "loopback"),
    ("TEST_STOPPING\n" + ERROR + "TEST_STOPPED\n", 1, 0, "peer"),
], ids=[
    "outside-shutdown", "completed-shutdown", "too-many-disconnects",
    "incomplete-shutdown", "earlier-stop-is-not-completion",
    "errors-around-shutdown", "peer-is-not-loopback",
])
def test_disconnect_allowance_is_limited_to_completed_shutdown(
    audit_module, tmp_path, text, unexpected_count, known_count, group
):
    log = tmp_path / "dut.log"
    log.write_text(text)
    unexpected, known = audit_module._serial_error_lines(
        f"{group}/example/test_example.py::test_example", log
    )
    assert len(unexpected) == unexpected_count
    assert len(known) == known_count


def test_watchdog_reset_is_unexpected(audit_module, tmp_path):
    log = tmp_path / "peer-device.log"
    log.write_text("TEST_STOPPING\nrst:0x8 (TG1WDT_SYS_RST),boot:0x8 (SPI_FAST_FLASH_BOOT)\n")
    unexpected, known = audit_module._serial_error_lines(
        "peer/usb_ncm/test_usb_ncm.py::test_usb_ncm", log
    )
    assert len(unexpected) == 1
    assert "TG1WDT_SYS_RST" in unexpected[0]
    assert not known
