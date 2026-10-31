"""Vendor full-speed bulk packet sizes: the four USB 2.0 allows work, the rest fail begin()."""

SIZES = (8, 16, 32, 64)


def test_vendor_packet_size(dut, peers, request):
    device = peers["device"]
    high_speed = "_hs_" in request.node.name
    try:
        device.write("refuse")
        device.expect_exact("REFUSED ok=1 sizes=5 heap=1\r\n", timeout=10)
        for size in SIZES:
            device.write(f"start {size}")
            device.expect_exact("STARTED ok=1\r\n", timeout=10)
            # High speed always uses 512; the constructor picks the full-speed size.
            packet = 512 if high_speed else size
            dut.write(f"start {packet}")
            dut.expect_exact(f"HOST_READY ok=1 in={packet} out={packet}\r\n", timeout=30)
            for n in sorted({1, size - 1, size, size + 1, 4 * size, 1000}):
                dut.write(f"exchange {n}")
                dut.expect_exact(f"EXCHANGE {n} ok=1\r\n", timeout=10)
            dut.write("stop")
            dut.expect_exact("STOPPED heap=1\r\n", timeout=30)
            device.write("stop")
            device.expect_exact("STOPPED heap=1\r\n", timeout=30)
    finally:
        dut.write("stop")
        device.write("stop")
