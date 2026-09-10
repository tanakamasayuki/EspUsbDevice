"""USB Audio headset: one device that is both a speaker (host -> device) and a
microphone (device -> host). Both directions must enumerate, start, and carry PCM
at the same time.

The preconditions are asked rather than awaited. This module was already a single
test, so its boot-banner waits worked - but only by being first by construction.
``HEADSET_DEVICE_READY`` is printed once by the device's setup() and the
``AUDIO_STREAM`` lines once by the host's connect callback; the device now answers
'?' with a ready flag and the host answers 'S' with the same stream report, so
nothing here depends on having seen boot.

The cases are named functions driven from a list. ``_streaming`` starts both
directions and resets the counters it then reads, so it establishes what it
needs; the order is not load-bearing.
"""

import time


def _both_streams_present(dut, device):
    """The device exposes both an OUT (speaker) and an IN (microphone) stream.

    Read from the descriptors, so it holds whether or not either stream has been
    started.
    """
    dut.write("S")
    dut.expect("AUDIO_STREAM .* dir=OUT ")
    dut.expect("AUDIO_STREAM .* dir=IN ")


def _ready_both_directions(dut, device):
    """A stable device that is ready in both directions.

    The 'i' command polls for up to 15 s, which tolerates the re-enumeration the
    device can do at startup, so this does not depend on boot timing.
    """
    dut.write("i")
    dut.expect("HOST_AUDIO addr=[1-9][0-9]* out=1 in=1", timeout=20)


def _streaming(dut, device):
    _ready_both_directions(dut, device)

    # Start both streams; the device sees both interfaces enabled by the host.
    dut.write("a")
    dut.expect_exact("HEADSET_START out=1 in=1")
    device.expect_exact("AUDIO_INTERFACE SPK 1")
    device.expect_exact("AUDIO_INTERFACE MIC 1")

    # Reset both byte counters, so what follows was produced by this case.
    dut.write("r")
    dut.expect_exact("HEADSET_RESET")
    device.write("r")
    device.expect_exact("HEADSET_RESET")

    # OUT: host sends speaker PCM, device receives it.
    dut.write("s")
    dut.expect("AUDIO_TX [1-9][0-9]*")
    device.expect("DEVICE_RX_AUDIO [1-9][0-9]*")

    # IN: the device streams a loud sawtooth continuously; give it time, then
    # confirm the host received non-silent PCM.
    time.sleep(0.5)
    dut.write("?")
    dut.expect("HOST_RX bytes=[1-9][0-9]* maxAbs=[1-9][0-9]*")

    # The device confirms both directions moved data.
    device.write("?")
    device.expect("HEADSET_ALIVE rx=[1-9][0-9]* tx=[1-9][0-9]*")


def test_usb_audio_headset(dut, peers):
    device = peers["device"]

    device.write("?")
    device.expect_exact("DEVICE_READY 1")

    for check in (_both_streams_present, _ready_both_directions, _streaming):
        check(dut, device)
