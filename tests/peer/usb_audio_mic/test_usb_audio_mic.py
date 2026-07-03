import time


def test_usb_audio_mic(dut, peers):
    """USB Audio source (microphone): the device streams PCM to the host. Start
    the input stream and confirm device -> host PCM arrives and is non-silent."""
    device = peers["device"]

    device.expect_exact("MIC_DEVICE_READY 1")

    # Wait for a stable, audio-input-ready device (the 'i' command polls up to
    # 15 s, tolerating startup re-enumeration), so this passes regardless of
    # boot timing / run order.
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
