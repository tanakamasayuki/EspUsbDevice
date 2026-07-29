import pexpect
import pytest


def _probe_device(device, pattern, retries=8, timeout=6):
    """Poll the device liveness probe until `pattern` matches, tolerating an
    in-progress boot or a lost probe byte. Raises on persistent failure (e.g. a
    reboot that resets the counters, or an unresponsive/hung device)."""
    last = None
    for _ in range(retries):
        device.write("?")
        try:
            device.expect(pattern, timeout=timeout)
            return
        except pexpect.TIMEOUT as err:
            last = err
    raise last


def test_usb_audio_speaker_uac2_enumeration(dut, peers):
    device = peers["device"]

    device.expect_exact("AUDIO_DEVICE_READY 1")
    dut.expect("AUDIO_OUT_READY addr=[0-9]+")
    # EspUsbHost 2.5.0 only decodes the UAC1 Type-I format descriptor.
    # It still verifies that the spec-correct UAC2 data and feedback endpoints
    # enumerate. Format decoding/stream start is gated on a UAC2 host parser.
    dut.expect("AUDIO_STREAM iface=[0-9]+ alt=1 ep=0x01 dir=OUT channels=0 bytes=0 bits=0 rate=0 rates=0 first=0 min=0 max=0 maxPacket=98 interval=1")
    dut.expect("AUDIO_STREAM iface=[0-9]+ alt=1 ep=0x81 dir=IN channels=0 bytes=0 bits=0 rate=0 rates=0 first=0 min=0 max=0 maxPacket=4 interval=1")


@pytest.mark.skip(reason="requires a UAC2-aware EspUsbHost parser and v2 control event queue")
def test_usb_audio_speaker_volume_flood(dut, peers):
    """Reproduce the crash seen on a real Windows host: dragging the volume
    slider sends a rapid burst of intermediate SET_CUR values. The host blasts
    rapid volume (then mute) changes; the device must keep running and must not
    reboot.

    The audio control callback used to run the user onEvent() on the 2048-byte
    Arduino USB event loop task; a burst of changes overflowed that stack and
    crashed. Audio events now dispatch on a dedicated loop with a generous stack,
    so the device must survive the burst."""
    device = peers["device"]

    # Order-independent setup: confirm the device is alive (tolerating an
    # in-progress boot), and make the host resolve the audio address before
    # flooding (the 'i' command waits for enumeration). This lets the test pass
    # standalone or after other tests, regardless of boot timing.
    _probe_device(device, "DEVICE_ALIVE vol=[0-9]+ mute=[0-9]+")
    # Wait for the host to hold a stable, audio-output-ready address (the device
    # can re-enumerate a few times at startup). The 'i' command blocks up to 15 s.
    dut.write("i")
    dut.expect("HOST_AUDIO addr=[1-9][0-9]* ready=1", timeout=20)

    dut.write("v")
    dut.expect_exact("VOLUME_FLOOD_BEGIN")
    # The SET_CUR requests actually reach the device.
    device.expect("DEV_VOL ch=[0-9]+ db=-?[0-9]+ n=[0-9]+")
    # The host completes the whole burst.
    dut.expect_exact("VOLUME_FLOOD_DONE", timeout=60)

    # Survival check: still the same running session (nonzero accumulated volume
    # events) and not reset. A reboot resets the counter to 0 (fails the [1-9]
    # match) or leaves the device unresponsive (both make _probe_device raise).
    _probe_device(device, "DEVICE_ALIVE vol=[1-9][0-9]* mute=[0-9]+")

    # Same again for rapid mute toggling, which uses the same control-callback
    # event path.
    dut.write("m")
    dut.expect_exact("MUTE_FLOOD_BEGIN")
    device.expect("DEV_MUTE ch=[0-9]+ m=[01] n=[0-9]+")
    dut.expect_exact("MUTE_FLOOD_DONE", timeout=60)

    _probe_device(device, "DEVICE_ALIVE vol=[0-9]+ mute=[1-9][0-9]*")
