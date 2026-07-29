import subprocess
from pathlib import Path


HERE = Path(__file__).parent
ROOT = HERE.parents[2]


def test_descriptor_model():
    output = HERE / "output"
    output.mkdir(exist_ok=True)
    binary = output / "descriptor_model_test"
    result = subprocess.run(
        [
            "g++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(ROOT / "src"),
            str(HERE / "descriptor_model_test.cpp"),
            str(ROOT / "src/internal/EspUsbDescriptorModel.cpp"),
            str(ROOT / "src/internal/EspUsbHidDescriptor.cpp"),
            "-o",
            str(binary),
        ],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stdout + result.stderr

    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
