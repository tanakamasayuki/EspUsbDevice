"""USB Audio source (microphone): the device streams PCM to the host.

The preconditions are asked rather than awaited. ``MIC_DEVICE_READY`` is printed
once by the device's setup(), so a test that did not run first would never see
it; the device now answers '?' with a ready flag, and the host answers 'S' with
the stream report its connect callback announces.

The cases are named functions driven from a list. ``_streaming`` resets the
counter it then reads, so the order is not load-bearing.
"""

import time


def _input_stream_present(dut, device):
    """One IN (microphone) stream, read from the descriptors."""
    dut.write("S")
    dut.expect("AUDIO_STREAM .* dir=IN ")


def _streaming(dut, device):
    # The 'i' command polls for up to 15 s, tolerating the re-enumeration the
    # device can do at startup, so this does not depend on boot timing.
    dut.write("i")
    dut.expect("HOST_AUDIO addr=[1-9][0-9]* ready=1", timeout=20)

    dut.write("a")
    dut.expect_exact("MIC_START 1")
    # The device sees its microphone streaming interface enabled by the host.
    device.expect_exact("AUDIO_INTERFACE MIC 1")

    dut.write("r")
    dut.expect_exact("MIC_RESET")

    # The device is generating a loud sawtooth continuously; give it time to
    # stream, then confirm the host received non-silent PCM.
    time.sleep(0.5)
    dut.write("?")
    dut.expect("HOST_RX bytes=[1-9][0-9]* maxAbs=[1-9][0-9]*")

    # And the device confirms it actually pushed samples out.
    device.write("?")
    device.expect("MIC_ALIVE tx=[1-9][0-9]*")


def test_usb_audio_microphone(dut, peers):
    device = peers["device"]

    device.write("?")
    device.expect_exact("DEVICE_READY 1")

    for check in (_input_stream_present, _streaming):
        check(dut, device)
