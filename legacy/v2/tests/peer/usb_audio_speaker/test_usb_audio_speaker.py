"""UAC1 speaker: streaming, and the control path under a burst of requests.

One test rather than three. The first read three lines the sketches print once
at boot - `AUDIO_DEVICE_READY`, `AUDIO_OUT_READY` and the `AUDIO_STREAM`
descriptor - so it only worked when it ran first, and the other two inherited the
enumeration it had waited for.

The device answers a liveness probe ('?') that now leads with the ready flag, and
the host's stream report is a command ('S') as well as a connect-time
announcement, so both facts can be asked for at any point.

The cases are named functions driven from a list. Each re-establishes the host's
audio address before it does anything, so the order is not load-bearing.
"""

import pexpect


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


def _audio_address(dut):
    """Make the host resolve a stable, audio-output-ready address.

    The device can re-enumerate a few times at startup, and only becomes
    audioOutputReady() once its streams are parsed, so the 'i' command polls for
    up to 15 s rather than reporting whatever address it holds.
    """
    dut.write("i")
    dut.expect("HOST_AUDIO addr=[1-9][0-9]* ready=1", timeout=20)


def _stream_descriptor(dut, device):
    """The streaming interface as the host parsed it out of the descriptors:
    one 48 kHz 16-bit mono OUT stream on alt=1, 98-byte packets at 1 ms."""
    dut.write("S")
    dut.expect(
        "AUDIO_STREAM iface=[0-9]+ alt=1 ep=0x01 dir=OUT channels=1 bytes=2 "
        "bits=16 rate=48000 rates=1 first=48000 min=0 max=0 maxPacket=98 interval=1"
    )


def _streaming(dut, device):
    _audio_address(dut)
    dut.write("a")
    dut.expect_exact("AUDIO_START 1")
    device.expect("AUDIO_INTERFACE PLAYBACK 1 alt=1")

    # Reset first, so the byte count below was produced by this send.
    device.write("r")
    device.expect_exact("DEVICE_AUDIO_RESET")
    dut.write("s")
    dut.expect("AUDIO_TX [1-9][0-9]*")
    device.expect("DEVICE_RX_AUDIO [1-9][0-9]*")


def _volume_flood(dut, device):
    """Reproduce the crash seen on a real Windows host: dragging the volume
    slider sends a rapid burst of intermediate SET_CUR values. The host blasts
    rapid volume (then mute) changes; the device must keep running and must not
    reboot.

    The audio control callback used to run the user onEvent() on the 2048-byte
    Arduino USB event loop task; a burst of changes overflowed that stack and
    crashed. Audio events now dispatch on a dedicated loop with a generous stack,
    so the device must survive the burst."""
    _probe_device(device, "DEVICE_ALIVE .* vol=[0-9]+ mute=[0-9]+")
    _audio_address(dut)

    dut.write("v")
    dut.expect_exact("VOLUME_FLOOD_BEGIN")
    # The SET_CUR requests actually reach the device.
    device.expect("DEV_VOL ch=[0-9]+ db=-?[0-9]+ n=[0-9]+")
    # The host completes the whole burst.
    dut.expect_exact("VOLUME_FLOOD_DONE", timeout=60)

    # Survival check: still the same running session (nonzero accumulated volume
    # events) and not reset. A reboot resets the counter to 0 (fails the [1-9]
    # match) or leaves the device unresponsive (both make _probe_device raise).
    _probe_device(device, "DEVICE_ALIVE .* vol=[1-9][0-9]* mute=[0-9]+")

    # Same again for rapid mute toggling, which uses the same control-callback
    # event path.
    dut.write("m")
    dut.expect_exact("MUTE_FLOOD_BEGIN")
    device.expect("DEV_MUTE ch=[0-9]+ m=[01] n=[0-9]+")
    dut.expect_exact("MUTE_FLOOD_DONE", timeout=60)

    _probe_device(device, "DEVICE_ALIVE .* vol=[0-9]+ mute=[1-9][0-9]*")


def _per_channel_controls(dut, device):
    """Mute and volume addressed to channel 1 rather than the master, read back
    through the same control interface and observed on the device."""
    _probe_device(device, "DEVICE_ALIVE .*")
    _audio_address(dut)

    dut.write("c")
    dut.expect_exact(
        "CHANNEL_CONTROL caps=1 set=1 get=1 mute=1 "
        "volume=-1536 range=-23040:0:256"
    )
    device.expect("DEV_MUTE ch=1 m=1 n=[1-9][0-9]*")
    device.expect("DEV_VOL ch=1 db=-1536 n=[1-9][0-9]*")


def test_usb_audio_speaker(dut, peers):
    device = peers["device"]

    # The precondition, asked rather than awaited: the device answers only once
    # the host has configured it.
    _probe_device(device, "DEVICE_READY 1")

    for check in (_stream_descriptor, _streaming, _volume_flood, _per_channel_controls):
        check(dut, device)
