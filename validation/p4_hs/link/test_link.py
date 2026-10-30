"""HS direct link of the 2-3/2-4 pair: enumeration in this combo's direction and VBUS on the Device side."""

import json
import re
import pathlib
import time

RESULTS = pathlib.Path(__file__).resolve().parents[2] / ".pytest-results"


def test_hs_link(dut, peers, request):
    device = peers["device"]
    results = {}

    def state():
        device.write("state")
        m = device.expect(r"STATE ready=(\d) bsesvld=(\d) gotgctl=([0-9a-f]+)\r\n", timeout=5)
        return {"ready": int(m.group(1)), "bsesvld": int(m.group(2)), "gotgctl": m.group(3).decode()}

    def enumerate_with(sensing):
        device.write(f"start {sensing}")
        device.expect_exact("STARTED 1\r\n", timeout=30)
        time.sleep(0.5)
        before = state()  # Host stopped: VBUS here is not switched by the Host's software.
        dut.write("start")
        dut.expect_exact("HOST_STARTED 1\r\n", timeout=30)
        dut.write("wait")
        m = dut.expect(r"DEVICE n=(\d+) vid=([0-9a-f]+) pid=([0-9a-f]+) speed=(\d+)\r\n", timeout=10)
        time.sleep(0.5)
        result = {"before_host": before, "after_host": state(), "connected": int(m.group(1)),
                  "vid": m.group(2).decode(), "pid": m.group(3).decode(), "speed": int(m.group(4))}
        dut.write("stop")
        dut.expect_exact("HOST_STOPPED\r\n", timeout=30)
        device.write("stop")
        device.expect_exact("STOPPED\r\n", timeout=30)
        return result

    try:
        for _ in range(2):
            results.setdefault("forced", []).append(enumerate_with(0))
            results.setdefault("sensing", []).append(enumerate_with(1))
    finally:
        RESULTS.mkdir(exist_ok=True)
        (RESULTS / f"hs-link-{re.sub(r'\W+', '-', request.node.name).strip('-')}.json").write_text(json.dumps(results, indent=1))
        dut.write("stop")
        device.write("stop")
    # usb_speed_t: 0 low, 1 full, 2 high.
    for r in results["forced"]:
        assert (r["connected"], r["vid"], r["pid"], r["speed"]) == (1, "1209", "0002", 2), r
