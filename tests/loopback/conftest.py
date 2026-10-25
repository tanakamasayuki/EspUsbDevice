import pytest

from usb_test_lifecycle import usb_test_session
from usb_test_matrix import check_item


def pytest_arduino_cli_skip_reason(item, combination):
    return check_item(item, combination, "loopback")


@pytest.fixture(autouse=True)
def usb_session(request, dut):
    with usb_test_session(dut, request.path.parent.name):
        yield
