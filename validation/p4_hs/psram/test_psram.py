"""PSRAM size of each P4 board on the bench, read on the board itself (the combo's primary)."""
import json
import pathlib
import re

RESULTS = pathlib.Path(__file__).resolve().parents[2] / ".pytest-results"


def test_psram(dut, request):
    dut.write("psram")
    m = dut.expect(r"PSRAM ok=(\d) size=(\d+) heap=(\d+) tested=(\d+) revision=(\d+)\r\n", timeout=120)
    result = dict(zip(("ok", "size", "heap", "tested", "revision"), map(int, m.groups())))
    name = re.sub(r"\W+", "-", request.node.name).strip("-")
    (RESULTS / f"psram-{name}.json").write_text(json.dumps(result))
    print(result)
    assert result["ok"] == 1
