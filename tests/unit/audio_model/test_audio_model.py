import subprocess
from pathlib import Path


HERE = Path(__file__).parent
ROOT = HERE.parents[2]


def test_audio_model():
    output = HERE / "output"
    output.mkdir(exist_ok=True)
    binary = output / "audio_model_test"
    result = subprocess.run(
        [
            "g++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(ROOT / "src"),
            str(HERE / "audio_model_test.cpp"),
            str(ROOT / "src/internal/EspUsbAudioModel.cpp"),
            str(ROOT / "src/internal/EspUsbAudioDescriptor.cpp"),
            str(ROOT / "src/internal/EspUsbAudioControl.cpp"),
            str(ROOT / "src/internal/EspUsbAudioRequest.cpp"),
            str(ROOT / "src/internal/EspUsbDescriptorModel.cpp"),
            "-o",
            str(binary),
        ],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stdout + result.stderr

    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
