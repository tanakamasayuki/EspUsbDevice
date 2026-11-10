"""Borrowed bulk IN streamed to the Linux PC at high speed.

The device resubmits fixed-size blocks from the completion callback, keeping one
or two blocks submitted. The PC reads them back in order, checks every block's
sequence number and measures the throughput. The figures are recorded for
comparison; the pass condition is that no block is lost, duplicated or out of
order.
"""
import json
import pathlib
import re
import time

import usb.core
import usb.util

RESULTS = pathlib.Path(__file__).resolve().parents[2] / ".pytest-results"
SERIAL = "espusb-bulk-stream"
BLOCK = 53 * 512
BLOCKS = 2048          # about 55 MB per run
READ = BLOCK * 512     # one libusb transfer (about 14 MB), split into URBs queued together


def find(timeout=10.0):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        for d in usb.core.find(find_all=True, idVendor=0x1209, idProduct=0x0001):
            try:
                if usb.util.get_string(d, d.iSerialNumber) == SERIAL:
                    return d
            except (usb.core.USBError, ValueError):
                pass
        time.sleep(0.01)
    raise AssertionError("bulk stream device did not enumerate on the PC")


def stream(dut, ep, depth, block):
    blocks = BLOCKS * BLOCK // block
    total = blocks * block
    dut.write(f"stream {blocks} {depth} {block}")
    dut.expect_exact(f"STREAMING 1 depth={depth} bytes={block}\r\n", timeout=5)
    data = bytearray()
    first = None
    while len(data) < total:
        chunk = ep.read(min(READ, total - len(data)), timeout=2000)
        if first is None:
            first = time.monotonic()
        data += chunk
    seconds = time.monotonic() - first
    dut.write("result")
    m = dut.expect(rb"RESULT done=(\d) blocks=(\d+) busy=(\d+) errors=(\d+) us=(\d+)\r\n", timeout=5)
    done, completed, busy, errors, us = map(int, m.groups())
    seqs = [int.from_bytes(data[i:i + 4], "little") for i in range(0, total, block)]
    return {
        "depth": depth, "block": block, "expected": blocks,
        "pc_mb_s": round((total - min(READ, total)) / seconds / 1e6, 2) if seconds else 0,
        "device_mb_s": round(total / us, 2) if us else 0,
        "in_order": seqs == list(range(blocks)),
        "done": done, "blocks": completed, "busy": busy, "errors": errors,
    }


def test_bulk_stream(dut):
    results = []
    try:
        dut.write("start")
        dut.expect_exact("STARTED 1\r\n", timeout=10)
        dev = find()
        assert dev.speed == usb.util.SPEED_HIGH
        dut.write("ready")
        dut.expect_exact("READY 1 speed=2\r\n", timeout=10)
        if dev.is_kernel_driver_active(0):
            dev.detach_kernel_driver(0)
        usb.util.claim_interface(dev, 0)
        intf = dev.get_active_configuration()[(0, 0)]
        ep = usb.util.find_descriptor(intf, custom_match=lambda e: e.bEndpointAddress & 0x80)
        # Large blocks as a streaming application uses them, then small ones,
        # where the gap between a completion and the next submission costs most.
        for block in (BLOCK, 4096):
            for depth in (1, 2, 1, 2):
                results.append(stream(dut, ep, depth, block))
        usb.util.release_interface(dev, 0)
    finally:
        RESULTS.mkdir(exist_ok=True)
        (RESULTS / "bulk-stream.json").write_text(json.dumps(results, indent=1))
        for r in results:
            print("BULK_STREAM", r)
        dut.write("stop")
    for r in results:
        assert (r["done"], r["blocks"], r["errors"], r["in_order"]) == (1, r["expected"], 0, True), r
