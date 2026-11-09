"""FS pad link of a two-board pair, checked below both USB stacks.

Each side drives or pulls its own D+/D- with the USB_WRAP test and pull
override registers and the other side reads its pads. A failure here means the
two native ports are not electrically joined, whatever the USB stacks do.
"""

import re


def test_pad_link(dut, peers):
    device = peers["device"]
    seen = []

    def read(board):
        board.write("read")
        m = board.expect(rb"PADS dp=(\d) dm=(\d)\r\n", timeout=5)
        return int(m.group(1)), int(m.group(2))

    try:
        for board in (dut, device):
            board.write("init")
            board.expect_exact("INIT 0\r\n", timeout=10)
        for name, source, sink in (("host->device", dut, device), ("device->host", device, dut)):
            sink.write("pulls 0 1 0 1")  # 15 kohm pull-downs, as a host port
            sink.expect_exact("PULLS 0 1 0 1\r\n", timeout=5)
            for dp, dm in ((1, 0), (0, 1), (0, 0), (1, 1)):
                source.write(f"drive {dp} {dm}")
                source.expect_exact(f"DRIVEN {dp} {dm}\r\n", timeout=5)
                seen.append((name, "drive", (dp, dm), read(sink)))
            # 1.5 kohm on D+ against the other side's pull-downs: what a host detects.
            source.write("pulls 1 0 0 0")
            source.expect_exact("PULLS 1 0 0 0\r\n", timeout=5)
            seen.append((name, "pullup", (1, 0), read(sink)))
            source.write("pulls 0 1 0 1")
            source.expect_exact("PULLS 0 1 0 1\r\n", timeout=5)
            seen.append((name, "idle", (0, 0), read(sink)))
    finally:
        for line in seen:
            print("PAD_LINK", *line)
    wrong = [s for s in seen if s[2] != s[3]]
    assert not wrong, wrong
