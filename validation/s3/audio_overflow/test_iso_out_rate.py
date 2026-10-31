"""Measure how many back-to-back isochronous OUT packets the Device takes in (no pass/fail on the rate)."""
import json
import pathlib
import re

RESULTS = pathlib.Path(__file__).resolve().parents[2] / ".pytest-results"


def test_iso_out_rate(dut, peers, request):
    device = peers["device"]
    results = []
    try:
        for alternate in (1, 2, 3, 4):
            device.write("start 1 65")
            device.expect(r"STARTED 1 [^\r]*\r\n", timeout=30)
            dut.write("start")
            packets = {int(m.group(1)): (int(m.group(2)), int(m.group(3)))
                       for m in (dut.expect(r"CHOICE alt=(\d) ep=\d+ bytes=(\d) packet=(\d+)\r\n", timeout=30) for _ in range(4))}
            dut.expect_exact("HOST_READY 1\r\n", timeout=30)
            bytes_, packet = packets[alternate]
            full = packet // (2 * bytes_) * (2 * bytes_)
            dut.write(f"alt {alternate}")
            dut.expect_exact("SELECTED 1\r\n", timeout=10)
            dut.write(f"send 200 {full}")
            dut.expect(r"SENT ok=1 next=\d+ lost=0\r\n", timeout=60)
            device.write("dump")
            m = device.expect(r"DUMP valid=1 [^\r]*transferred=(\d+)[^\r]*\r\n", timeout=10)
            results.append({"alt": alternate, "packet": full, "sent": 200, "received": int(m.group(1)) // full})
            dut.write("pause")
            dut.expect_exact("PAUSED 1\r\n", timeout=10)
            for board in (dut, device):
                board.write("stop")
                board.expect_exact("STOPPED\r\n", timeout=30)
    finally:
        name = re.sub(r"\W+", "-", request.node.name).strip("-")
        (RESULTS / f"iso-out-rate-{name}.json").write_text(json.dumps(results, indent=1))
    print(results)
