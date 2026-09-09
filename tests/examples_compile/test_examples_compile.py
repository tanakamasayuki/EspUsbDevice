import shutil
import subprocess
import re
from pathlib import Path

import pytest


REPO_ROOT = Path(__file__).resolve().parents[2]
EXAMPLES_DIR = REPO_ROOT / "examples"


def example_profiles():
    cases = []
    for sketch in sorted(EXAMPLES_DIR.glob("*/*.ino")):
        example_dir = sketch.parent
        manifest = (example_dir / "sketch.yaml").read_text(encoding="utf-8")
        profiles = re.findall(r"^  ([A-Za-z0-9_]+):\s*$", manifest, re.MULTILINE)
        cases.extend((example_dir, profile) for profile in profiles)
    return cases


@pytest.mark.parametrize(
    ("example_dir", "profile"),
    example_profiles(),
    ids=lambda value: value.name if isinstance(value, Path) else value,
)
def test_example_compile(example_dir, profile):
    if shutil.which("arduino-cli") is None:
        pytest.skip("arduino-cli is not available")

    result = subprocess.run(
        ["arduino-cli", "compile", "--profile", profile, str(example_dir)],
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    # A negative return code is a signal, not a compiler verdict: something
    # outside this run killed arduino-cli. That has happened here - a
    # neighbouring project on the same machine ran `pkill -x arduino-cli` and
    # took two of these with it, each reporting -15 with no diagnostic in the
    # captured output. Say so, because the alternative is spending time looking
    # for a compile error in a sketch that compiles.
    if result.returncode < 0:
        raise AssertionError(
            f"arduino-cli was killed by signal {-result.returncode} while compiling "
            f"{example_dir.name} for {profile}; this is not a compile failure. "
            "Something outside this test terminated it - re-run before "
            f"investigating.\ncaptured output:\n{result.stdout}"
        )
    assert result.returncode == 0, result.stdout
