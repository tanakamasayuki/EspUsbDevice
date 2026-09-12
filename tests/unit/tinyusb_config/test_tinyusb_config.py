import subprocess
from pathlib import Path


HERE = Path(__file__).parent
ROOT = HERE.parents[2]


def _compile(target: str, extra: list[str] | None = None, name: str = "") -> subprocess.CompletedProcess:
    output = HERE / "output"
    output.mkdir(exist_ok=True)
    binary = output / f"tinyusb_config_{target.lower()}{name}"
    return subprocess.run(
        [
            "g++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            f"-DCONFIG_IDF_TARGET_{target}=1",
            *(extra or []),
            "-I",
            str(ROOT / "src"),
            str(HERE / "tinyusb_config_test.cpp"),
            "-o",
            str(binary),
        ],
        capture_output=True,
        text=True,
    )


def _compile_and_run(target: str) -> None:
    result = _compile(target)
    assert result.returncode == 0, result.stdout + result.stderr

    binary = HERE / "output" / f"tinyusb_config_{target.lower()}"
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr


def test_library_owned_tinyusb_config():
    for target in ("ESP32S2", "ESP32S3", "ESP32P4"):
        _compile_and_run(target)


def test_class_buffer_sizes_are_overridable():
    """A sketch's build_opt.h must win over the library's defaults.

    This is the one capability the core's precompiled TinyUSB cannot offer, and
    it only exists because every size is behind #ifndef. Compiling with the flag
    an Arduino build_opt.h would produce is the check: the header keeps the
    override, so the ESP32-P4 static_assert for the 8 KiB default fails.
    """
    result = _compile(
        "ESP32P4", ["-DCFG_TUD_VENDOR_TX_BUFSIZE=4096"], name="_override"
    )
    assert result.returncode != 0
    assert "P4 vendor TX FIFO is 8 KiB" in result.stderr


def test_oversized_fifo_is_a_build_failure():
    """64 KiB is the size that looked like it should work and silently did not.

    tu_edpt_stream_init() takes the size as uint16_t, so 65536 arrives as a FIFO
    depth of 0 - the device reports ready and never mounts. Nothing about that
    points at the flag that caused it, so the header refuses the build instead.
    """
    result = _compile(
        "ESP32P4", ["-DCFG_TUD_VENDOR_TX_BUFSIZE=65536"], name="_oversize"
    )
    assert result.returncode != 0
    assert "cannot exceed 32768 bytes" in result.stderr
