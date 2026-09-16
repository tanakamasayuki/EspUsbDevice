import json
import subprocess
from pathlib import Path


ROOT = Path(__file__).parents[3]


def test_tinyusb_pin_metadata_is_explicit():
    metadata = json.loads(
        (
            ROOT / "third_party" / "tinyusb" / "UPSTREAM.json"
        ).read_text(encoding="utf-8")
    )

    assert metadata["schema"] == 1
    assert metadata["repository"] == "hathach/tinyusb"
    assert len(metadata["commit"]) == 40
    assert metadata["tinyusb_version"]
    assert metadata["reviewed_with_arduino_esp32"]
    assert metadata["selection_reason"]


def test_tinyusb_build_tree_matches_pinned_upstream():
    result = subprocess.run(
        ["python3", str(ROOT / "tools" / "verify_tinyusb_vendor.py")],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stdout + result.stderr


def test_tinyusb_update_dry_run_is_clean():
    result = subprocess.run(
        ["python3", str(ROOT / "tools" / "update_tinyusb_vendor.py")],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    # The count comes from the manifest rather than a literal, so adding a
    # vendored class does not fail this test for the wrong reason - but a file
    # silently dropped from the build tree still does, which is the point.
    selected = len(
        [
            line
            for line in (
                ROOT / "third_party" / "tinyusb" / "BUILD_FILES.txt"
            ).read_text(encoding="utf-8").splitlines()
            if line.strip() and not line.lstrip().startswith("#")
        ]
    )
    assert f"0 of {selected} selected files differ" in result.stdout
