import time


def test_usb_audio_headset(dut, peers):
    """USB Audio headset: one device that is both a speaker (host -> device) and
    a microphone (device -> host). Confirm both directions enumerate, start, and
    carry PCM at the same time."""
    device = peers["device"]

    device.expect_exact("HEADSET_DEVICE_READY 1")

    # The device exposes both an OUT (speaker) and an IN (microphone) stream.
    dut.expect("AUDIO_STREAM .* dir=OUT ")
    dut.expect("AUDIO_STREAM .* dir=IN ")

    # Wait for a stable device that is ready in both directions (tolerates
    # startup re-enumeration), so this passes regardless of boot timing / order.
    dut.write("i")
    dut.expect("HOST_AUDIO addr=[1-9][0-9]* out=1 in=1", timeout=20)

    # Start both streams; the device sees both interfaces enabled by the host.
    dut.write("a")
    dut.expect_exact("HEADSET_START out=1 in=1")
    device.expect_exact("AUDIO_INTERFACE SPK 1")
    device.expect_exact("AUDIO_INTERFACE MIC 1")

    # Reset both byte counters.
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
