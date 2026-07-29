import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]


def _first_party_sources():
    patterns = (
        "src/*.cpp",
        "src/*.h",
        "src/internal/*.cpp",
        "src/internal/*.h",
        "src/keymap/*.cpp",
        "src/keymap/*.h",
    )
    for pattern in patterns:
        yield from ROOT.glob(pattern)


def test_arduino_tinyusb_core_dependency_does_not_return():
    forbidden = {
        "Arduino USB umbrella header": re.compile(
            r"#\s*include\s*[<\"]USB\.h[>\"]"
        ),
        "Arduino TinyUSB HAL header": re.compile(
            r"#\s*include\s*[<\"]esp32-hal-tinyusb\.h[>\"]"
        ),
        "Arduino descriptor loader": re.compile(
            r"\btinyusb_enable_interface(?:2)?\b"
        ),
        "Arduino endpoint allocator": re.compile(
            r"\btinyusb_get_free_(?:in|out|duplex)_endpoint\b"
        ),
        "Arduino TinyUSB initializer": re.compile(r"\btinyusb_init\s*\("),
        "Arduino Audio descriptor loader": re.compile(
            r"\btusb_audio_load_descriptor\s*\("
        ),
    }

    findings = []
    for path in _first_party_sources():
        text = path.read_text(encoding="utf-8")
        for label, pattern in forbidden.items():
            for match in pattern.finditer(text):
                line = text.count("\n", 0, match.start()) + 1
                findings.append(
                    f"{path.relative_to(ROOT)}:{line}: {label}"
                )

    assert not findings, "\n".join(findings)


def test_removed_audio_card_sources_do_not_return():
    removed = (
        ROOT / "src" / "EspUsbDeviceAudio.cpp",
        ROOT / "src" / "EspUsbDeviceAudioDescriptors.h",
    )
    assert not [str(path.relative_to(ROOT)) for path in removed if path.exists()]


def test_first_party_audio_has_no_espressif_derivation_notice():
    audio_sources = (
        ROOT / "src" / "EspUsbAudio.cpp",
        *sorted((ROOT / "src" / "internal").glob("EspUsbAudio*")),
    )
    findings = []
    for path in audio_sources:
        text = path.read_text(encoding="utf-8")
        for line_number, line in enumerate(text.splitlines(), start=1):
            if re.search(
                r"(?:copyright|derived|based on).*espressif",
                line,
                re.IGNORECASE,
            ):
                findings.append(
                    f"{path.relative_to(ROOT)}:{line_number}: {line.strip()}"
                )

    assert not findings, "\n".join(findings)
