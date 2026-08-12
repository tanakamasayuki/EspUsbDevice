import re
import time


# The reported symptom is that device->host traffic stops permanently after a
# while of continuous streaming and only a restart recovers it. These tests run
# the stream for 30s and 300s and assert the link both keeps moving data and is
# still usable afterwards.

_SOAK_RE = (
    r"SOAK connect=1 bytes=(\d+) ms=(\d+) kbps=(\d+) maxIdleMs=(\d+) "
    r"stalled=(\d+) disconnected=(\d)"
)
_STATS_RE = (
    r"SOAK_STATS ready=(\d) link=(\d) netif=(\d) rxNtb=(\d+) rxFrames=(\d+) "
    r"tx=(\d+) txFail=(\d+) heap=(\d+) block=(\d+)"
)


def _wait_device_link(device, timeout=15):
    deadline = time.monotonic() + timeout
    while True:
        device.write("?")
        match = device.expect(
            r"DEVICE_READY ip=192\.168\.7\.1 link=(\d)",
            timeout=min(2, max(0.1, deadline - time.monotonic())),
        )
        if int(match.group(1)) == 1:
            return
        if time.monotonic() >= deadline:
            raise AssertionError(f"USB NCM device link did not come up within {timeout}s")
        time.sleep(0.1)


def _attach(dut, timeout=30):
    """Attach the host netif, retrying until the device has enumerated.

    Deliberately not keyed on HOST_CONNECTED: that line is printed once per
    connection, so the second test in this file would wait for a message that
    already went by.
    """
    deadline = time.monotonic() + timeout
    while True:
        dut.write("a")
        match = dut.expect(r"NETWORK_ATTACH ok=(\d)", timeout=10)
        if int(match.group(1)) == 1:
            break
        if time.monotonic() >= deadline:
            raise AssertionError(f"host did not attach the USB netif within {timeout}s")
        time.sleep(0.5)

    # Poll for the lease instead of waiting for the auto-report, which only
    # fires when the address changes and so stays silent for the second test.
    deadline = time.monotonic() + timeout
    while True:
        dut.write("p")
        match = dut.expect(r"NETWORK_IP ip=(\d+\.\d+\.\d+\.\d+)", timeout=10)
        if match.group(1).startswith(b"192.168.7."):
            return
        if time.monotonic() >= deadline:
            raise AssertionError(f"host got no 192.168.7.x lease within {timeout}s")
        time.sleep(0.5)


def _run_soak(dut, device, command, duration_s):
    _wait_device_link(device)
    _attach(dut)

    dut.write(command)
    dut.expect_exact("SOAK start")
    soak = dut.expect(_SOAK_RE, timeout=duration_s + 60)
    stats = dut.expect(_STATS_RE, timeout=15)
    print("soak:", soak.group(0))
    print("stats:", stats.group(0))

    # Device-side view: if the host stalled while the device kept pushing, the
    # fault is on the host side, not in EspUsbDevice's TX path.
    device.write("s")
    state = device.expect(
        r"DEVICE_STATE link=(\d) net=(\d) ip=\S+ sink=(\d+) source=(\d+) "
        r"writeFails=(\d+) canXmit=(\d) canXmitSmall=(\d) heap=(\d+) block=(\d+)",
        timeout=15,
    )
    print("device:", state.group(0))
    device.write("x")
    dwc2 = device.expect(r"DWC2_GLOBAL [^\n]+", timeout=15)
    print("dwc2:", dwc2.group(0))
    for _ in range(3):
        try:
            ep = device.expect(r"DWC2_EP\d [^\n]+", timeout=2)
            print("dwc2:", ep.group(0))
        except Exception:
            break
    # A wedged NCM transmit state machine shows up as a fresh connection that
    # either cannot be made or delivers nothing.
    dut.write("v")
    recover = dut.expect(r"RECOVER connect=(\d) bytes=(\d+)", timeout=30)
    print("recover:", recover.group(0))

    return soak, stats, state, recover


def _assert_healthy(soak, state, recover, duration_s):
    bytes_moved = int(soak.group(1))
    max_idle_ms = int(soak.group(4))
    stalled_s = int(soak.group(5))
    disconnected = int(soak.group(6))
    can_xmit = int(state.group(6))
    recovered = int(recover.group(1))
    recovered_bytes = int(recover.group(2))

    # 3.2 Mbps is the measured baseline for this direction; require a tenth of
    # it so a slow-but-alive link is not reported as the permanent stop.
    minimum = 400_000 * duration_s // 10
    assert bytes_moved > minimum, (
        f"device->host throughput collapsed: {bytes_moved} bytes in {duration_s}s"
    )
    assert disconnected == 0, "device closed the TCP source mid-soak"
    assert stalled_s == 0, f"stream was still stalled at the end of the soak ({stalled_s}s)"
    assert max_idle_ms < 5000, f"device->host stream stalled for {max_idle_ms} ms"
    assert can_xmit == 1, "tud_network_can_xmit() stuck false: NCM transmit path is wedged"
    assert recovered == 1 and recovered_bytes > 0, (
        "USB network did not recover after the soak"
    )


def test_usb_ncm_soak_30s(dut, peers):
    device = peers["device"]
    soak, _stats, state, recover = _run_soak(dut, device, "1", 30)
    _assert_healthy(soak, state, recover, 30)


def test_usb_ncm_soak_300s(dut, peers):
    device = peers["device"]
    soak, _stats, state, recover = _run_soak(dut, device, "2", 300)
    _assert_healthy(soak, state, recover, 300)
