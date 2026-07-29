import subprocess
from pathlib import Path


HERE = Path(__file__).parent
ROOT = HERE.parents[2]


def _compile(target: str) -> None:
    output = HERE / "output"
    output.mkdir(exist_ok=True)
    binary = output / f"tinyusb_config_{target.lower()}"
    result = subprocess.run(
        [
            "g++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            f"-DCONFIG_IDF_TARGET_{target}=1",
            "-I",
            str(ROOT / "src"),
            str(HERE / "tinyusb_config_test.cpp"),
            "-o",
            str(binary),
        ],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stdout + result.stderr

    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr


def test_library_owned_tinyusb_config():
    for target in ("ESP32S2", "ESP32S3", "ESP32P4"):
        _compile(target)
