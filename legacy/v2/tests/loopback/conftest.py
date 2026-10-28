import pytest

from usb_test_lifecycle import usb_test_session


@pytest.fixture(autouse=True)
def usb_session(request, dut):
    with usb_test_session(dut, request.path.parent.name):
        yield
