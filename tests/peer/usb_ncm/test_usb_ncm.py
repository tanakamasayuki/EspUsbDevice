"""CDC-NCM peer test, EspUsbDevice-repo copy.

DUT = the USB host (EspUsbHost, ``usb_ncm.ino``); the peer = the EspUsbDevice
CDC-NCM device (``peer_device/``). This is intentionally NOT the same as the
EspUsbHost-repo copy (which checks enumeration protocol + a single HTTP GET).
Here the angle is finer / different:

  - enumeration_endpoints  the enumerated descriptor is structurally valid
                           (separate control/data interfaces, active alt, three
                           distinct endpoints with correct directions)
  - frame_stats            transport layer: a real transfer moves frames in BOTH
                           directions with zero TX failures, and the DHCP lease
                           is a real client address (not the gateway .1)
  - device_observes_request  device-side view: the device's own web server
                             actually served the host's request
"""

import re
import time

ENUM_RE = re.compile(
    r"NCM_ENUM count=[1-9][0-9]* protocol=CDC-NCM complete=1 "
    r"ctrl=(\d+) data=(\d+) alt=(\d+) "
    r"in=0x([0-9a-fA-F]+) out=0x([0-9a-fA-F]+) notify=0x([0-9a-fA-F]+)"
)

STATS_RE = re.compile(
    r"NETWORK_STATS ready=(\d+) link=(\d+) netif=(\d+) rxNtb=(\d+) "
    r"rxFrames=(\d+) tx=(\d+) txFail=(\d+) ip=([0-9.]+)"
)

DEVICE_STATS_RE = re.compile(
    r"DEVICE_STATS link=(\d+) net=(\d+) ip=([0-9.]+) served=(\d+)"
)


def _text(group):
    # pytest-embedded matches against a bytes buffer, so regex groups come back
    # as bytes; int() accepts bytes but str comparisons do not, so decode here.
    return group.decode() if isinstance(group, bytes) else group


def _read_stats(dut):
    """Ask the host for its NCM transport counters and parse them."""
    dut.write("d")
    m = dut.expect(STATS_RE, timeout=10)
    return {
        "ready": int(m.group(1)),
        "link": int(m.group(2)),
        "netif": int(m.group(3)),
        "rxNtb": int(m.group(4)),
        "rxFrames": int(m.group(5)),
        "txFrames": int(m.group(6)),
        "txFail": int(m.group(7)),
        "ip": _text(m.group(8)),
    }


def _wait_device_link(device, timeout=15):
    """Wait for TinyUSB to observe the host's SET_CONFIGURATION request.

    The peer is intentionally uploaded and started after the host. Its serial
    console can therefore become ready slightly before USB enumeration reaches
    the configured state; a one-shot link check would race that transition.
    """
    deadline = time.monotonic() + timeout
    while True:
        device.write("?")
        m = device.expect(
            r"DEVICE_READY ip=192\.168\.7\.1 link=(\d)",
            timeout=min(2, max(0.1, deadline - time.monotonic())),
        )
        if int(m.group(1)) == 1:
            return
        if time.monotonic() >= deadline:
            raise AssertionError(f"USB NCM device link did not come up within {timeout}s")
        time.sleep(0.1)


def _ensure_attached(dut, device):
    """Bring the DHCP-client netif up regardless of prior test state.

    DUT fixtures are module-scoped, so an earlier test may already have attached
    the netif; the host's attach is not idempotent, so only attach when it is not
    up yet. Returns a fresh stats snapshot to use as a baseline.
    """
    device.write("?")
    device.expect_exact("DEVICE_READY ip=192.168.7.1")

    stats = _read_stats(dut)
    if stats["netif"] == 0:
        dut.write("a")
        dut.expect(r"NETWORK_ATTACH ok=\d")
        dut.expect(r"ip=192\.168\.7\.\d", timeout=30)
        stats = _read_stats(dut)
    return stats


def test_usb_ncm_enumeration_endpoints(dut, peers):
    """Finer than the protocol-only enumeration check: the host must have parsed
    a structurally valid CDC-NCM function."""
    device = peers["device"]
    _wait_device_link(device)

    dut.expect_exact("HOST_CONNECTED")
    dut.write("i")
    m = dut.expect(ENUM_RE, timeout=10)
    ctrl = int(m.group(1))
    data = int(m.group(2))
    alt = int(m.group(3))
    ep_in = int(m.group(4), 16)
    ep_out = int(m.group(5), 16)
    ep_notify = int(m.group(6), 16)

    # CDC-NCM = a control interface (notification EP) + a data interface (bulk).
    assert ctrl != data, (ctrl, data)
    # The bulk endpoints live on the data interface's alternate setting 1.
    assert alt == 1, alt
    # Three endpoints, all allocated and distinct.
    assert ep_in and ep_out and ep_notify, (ep_in, ep_out, ep_notify)
    assert len({ep_in, ep_out, ep_notify}) == 3, (ep_in, ep_out, ep_notify)
    # Directions: bulk IN and the interrupt notification are IN (0x80 set);
    # bulk OUT is an OUT endpoint (0x80 clear).
    assert ep_in & 0x80, hex(ep_in)
    assert ep_notify & 0x80, hex(ep_notify)
    assert not (ep_out & 0x80), hex(ep_out)


def test_usb_ncm_frame_stats(dut, peers):
    """Transport-layer perspective: link/netif up, the lease is a real client
    address, and a transfer moves frames in BOTH directions with no TX fails."""
    device = peers["device"]
    before = _ensure_attached(dut, device)

    assert before["ready"] == 1, before
    assert before["link"] == 1, before
    assert before["netif"] == 1, before
    # A real client lease, not the device's own gateway address.
    assert before["ip"].startswith("192.168.7."), before["ip"]
    assert before["ip"] != "192.168.7.1", before["ip"]

    # Drive real traffic: request out (TX), response in (RX).
    dut.write("g")
    dut.expect_exact("HTTP_GET code=200 body=ESPUSB_NCM_OK")

    after = _read_stats(dut)
    assert after["txFrames"] > before["txFrames"], (before, after)
    assert after["rxFrames"] > before["rxFrames"], (before, after)
    assert after["txFail"] == 0, after
    assert after["link"] == 1 and after["netif"] == 1, after


def test_usb_ncm_device_observes_request(dut, peers):
    """Device-side perspective (only possible from this repo's peer): after the
    host fetches the page, the DEVICE's own web server must report that it served
    the request, closing the loop from the device end."""
    device = peers["device"]
    _ensure_attached(dut, device)

    device.write("s")
    m0 = device.expect(DEVICE_STATS_RE, timeout=10)
    served_before = int(m0.group(4))

    dut.write("g")
    dut.expect_exact("HTTP_GET code=200 body=ESPUSB_NCM_OK")

    device.write("s")
    m1 = device.expect(DEVICE_STATS_RE, timeout=10)
    link = int(m1.group(1))
    net = int(m1.group(2))
    served_after = int(m1.group(4))

    # The device actually handled the HTTP request over the USB NCM link.
    assert served_after > served_before, (served_before, served_after)
    # And its link/lwIP netif were up while doing so.
    assert link == 1 and net == 1, m1.group(0)
