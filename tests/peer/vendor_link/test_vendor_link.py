"""Vendor over a high-speed link: MPS 512, copied echo and borrowed loans at packet boundaries, restart."""

COPY = (1, 511, 512, 513, 4095, 4096, 4097)
BORROWED = (1, 511, 512, 513, 4097, 65535, 65536, 65537)


def test_vendor_link(dut, peers):
    device = peers["device"]

    def start(mode):
        device.write(f"start {mode}")
        device.expect_exact("STARTED 1\r\n", timeout=30)
        dut.write("start")
        # usb_speed_t 2 is high speed; a fall back to full speed fails here.
        dut.expect_exact("HOST_READY ok=1 speed=2 in=512 out=512\r\n", timeout=30)
        device.write("link")
        device.expect_exact(f"LINK ready=1 maxrx={int(mode == 'borrowed')} maxtx=1\r\n", timeout=5)

    def stop():
        dut.write("stop")
        dut.expect_exact("STOPPED heap=1\r\n", timeout=30)
        device.write("stop")
        device.expect_exact("STOPPED 1 heap=1\r\n", timeout=30)

    try:
        for _ in range(3):
            start("copy")
            for n in COPY:
                dut.write(f"exchange {n}")
                dut.expect_exact(f"EXCHANGE {n} ok=1\r\n", timeout=15)
            device.write("echoed")
            device.expect_exact(f"ECHOED {sum(COPY)} bad=0\r\n", timeout=5)
            stop()

            start("borrowed")
            for n in BORROWED:
                device.write(f"rx {n}")
                device.expect_exact(f"RX_ARMED {n} ok=1\r\n", timeout=5)
                dut.write(f"send {n}")
                dut.expect_exact(f"SENT {n} ok=1\r\n", timeout=15)
                device.write(f"rx_done {n}")
                device.expect_exact(f"RX_DONE {n} ok=1 actual={n} completions=1\r\n", timeout=15)
                dut.write(f"expect {n}")
                dut.expect_exact(f"EXPECTING {n}\r\n", timeout=5)
                device.write(f"tx {n}")
                device.expect_exact(f"TX_DONE {n} ok=1 completions=1\r\n", timeout=15)
                dut.write(f"received {n}")
                dut.expect_exact(f"RECEIVED {n} ok=1\r\n", timeout=15)
            device.write("cancel")
            device.expect_exact("CANCEL ok=1 requested=1 completions=1 status=cancelled id=1\r\n", timeout=15)
            device.write("end_pending")
            m = device.expect(r"END_PENDING ok=(\d) ended=(\d) at_end=(\d) completions=(\d) status=(\w+)\r\n", timeout=15)
            assert m.group(1, 2, 4, 5) == (b"1", b"1", b"1", b"stopped"), m.group(0)
            stop()
    finally:
        dut.write("stop")
        device.write("stop")
