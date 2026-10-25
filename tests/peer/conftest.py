import pytest

from usb_test_lifecycle import usb_test_session


@pytest.fixture(autouse=True)
def usb_session(request, dut, peers):
    # Request peers even though startup only writes to dut: it guarantees that
    # peer upload/connect has finished before G can enable the host.
    with usb_test_session(dut, request.path.parent.name, peers.values()):
        yield
