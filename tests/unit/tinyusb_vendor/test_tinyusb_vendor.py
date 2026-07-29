import subprocess
from pathlib import Path


ROOT = Path(__file__).parents[3]


def test_tinyusb_build_tree_matches_pinned_upstream():
    result = subprocess.run(
        ["python3", str(ROOT / "tools" / "verify_tinyusb_vendor.py")],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stdout + result.stderr
