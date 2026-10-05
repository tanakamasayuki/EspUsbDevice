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
