def test_loopback_usb_ncm(dut):
    """CDC-NCM device-to-host soak on one P4.

    The failure this guards against is a permanent stop of the bulk IN path
    under sustained traffic, so the sketch fails on a run of seconds that moved
    no data and on `tud_network_can_xmit()` still being false at the end - not
    on an average, which a link that dies near the finish would still pass.
    """
    dut.expect_exact("TEST_BEGIN loopback_usb_ncm")
    dut.expect_exact("HOST_READY fs")
    dut.expect_exact("DEVICE_READY")
    dut.expect_exact("HOST_DEVICE")
    dut.expect_exact("NCM_OPEN ok=1")
    dut.expect_exact("NCM_LINK ok=1")

    soak = dut.expect(
        r"NCM_SOAK txBytes=(\d+) rxBytes=(\d+) txFrames=(\d+) rxFrames=(\d+) "
        r"ms=(\d+) kbps=(\d+) stalled=(\d+) drainMs=(\d+) canXmit=(\d)",
        timeout=90,
    )
    print("soak:", soak.group(0))

    dut.expect_exact("TEST_END ok")
    assert dut.expect_exact(["OK", "NG"]) == b"OK"
