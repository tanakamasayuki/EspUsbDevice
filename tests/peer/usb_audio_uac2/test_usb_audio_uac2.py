"""USB Audio Class 2.0 peer test.

DUT = the USB host (EspUsbHost 2.7.1, ``usb_audio_uac2.ino``); the peer = an
EspUsbDevice UAC2 headset (``peer_device/``). Until EspUsbHost gained UAC2 support
this device could only be checked against its own descriptors; this is the first
test that drives it with a real UAC2 host.

Deliberately a different angle from the ``usb_audio_uac2`` copy in the EspUsbHost
repository, which asserts what the *host* learned. Here the assertions are on the
device: that the control state the host wrote is the state the device applied
(both the master and the logical channel of the Feature Unit, read back through
the device's own getters, not the host's), that the Clock Source entity - not a
UAC1 endpoint request - is where the sample rate lives, and that both isochronous
directions actually carry PCM while the asynchronous playback interface's feedback
endpoint paces the host.

Not covered: switching between sample rates. The descriptor builder emits one
alternate setting per direction and therefore exactly one rate, so the Clock
Source has nothing to switch between; the ``RANGE`` encoder's multi-subrange path
is covered on the host side by ``tests/unit/audio_model``.
"""

import time

import pexpect

VOLUME_RANGE = "-23040:0:256"  # -90 dB .. 0 dB in 1 dB steps, 1/256 dB units


def _probe_device(device, pattern, retries=8, timeout=6):
    """Poll the device liveness probe until `pattern` matches, tolerating an
    in-progress boot or a lost probe byte."""
    last = None
    for _ in range(retries):
        device.write("?")
        try:
            device.expect(pattern, timeout=timeout)
            return
        except pexpect.TIMEOUT as err:
            last = err
    raise last


def _sync(dut, device):
    """Bring both boards to a known state: device alive, host holding a stable
    address that is ready in both directions."""
    _probe_device(device, r"UAC2_ALIVE ")
    dut.write("i")
    dut.expect("HOST_AUDIO addr=[1-9][0-9]* out=1 in=1", timeout=20)


def _device_state(device):
    """The device's own view of its control state, as {field: str}.

    pytest-embedded matches against the raw serial stream, so the groups come
    back as bytes; they are decoded here rather than at every comparison.
    """
    device.write("s")
    match = device.expect(
        r"UAC2_STATE proto=(\w+) rate=(\d+) master_mute=(\d) master_vol=(-?\d+) "
        r"ch1_mute=(\d) ch1_vol=(-?\d+) cap_mute=(\d) cap_vol=(-?\d+) "
        r"range=(-?\d+:-?\d+:-?\d+)",
        timeout=10,
    )
    fields = (
        "proto", "rate", "master_mute", "master_vol",
        "ch1_mute", "ch1_vol", "cap_mute", "cap_vol", "range",
    )
    state = {name: match.group(i + 1).decode() for i, name in enumerate(fields)}
    state["line"] = match.group(0).decode()
    return state


def test_usb_audio_uac2_enumeration(dut, peers):
    """The UAC2 descriptors the device emits are the ones a UAC2 host needs.

    Protocol 0x20 on both streaming interfaces, a Clock Source entity behind
    bTerminalLink carrying the sample rate (UAC2 Format Type I descriptors have
    none), Feature Units with the 4-byte / 2-bit bmaControls layout, and exactly
    two streams - the asynchronous playback interface's explicit feedback IN
    endpoint must not look like a third.
    """
    device = peers["device"]

    device.expect_exact("UAC2_DEVICE_READY 1 proto=uac2")
    dut.expect("AUDIO_OUT_READY addr=[1-9][0-9]*", timeout=20)
    dut.expect("AUDIO_IN_READY addr=[1-9][0-9]*", timeout=20)
    _sync(dut, device)

    # Asked for after enumeration, not from the connect callback: under UAC2 the
    # rates arrive from an asynchronous class request to the clock entity.
    dut.write("d")
    dut.expect(
        r"AUDIO_STREAM iface=\d+ alt=1 ep=0x0[0-9a-f] dir=OUT channels=1 bytes=2 "
        r"bits=16 rate=48000 rates=1 min=48000 max=48000 proto=0x20 "
        r"terminal=[1-9]\d* clock=[1-9]\d* startable=1"
    )
    dut.expect(
        r"AUDIO_STREAM iface=\d+ alt=1 ep=0x8[0-9a-f] dir=IN channels=1 bytes=2 "
        r"bits=16 rate=48000 rates=1 min=48000 max=48000 proto=0x20 "
        r"terminal=[1-9]\d* clock=[1-9]\d* startable=1"
    )
    dut.expect_exact("AUDIO_STREAM_COUNT 2")

    # One Feature Unit per direction, both with UAC2's 4-byte control stride and
    # mute + volume declared host-programmable (2-bit field 0b11).
    dut.write("u")
    dut.expect(
        r"AUDIO_UNIT unit=[1-9]\d* source=\d+ channels=1 control_size=4 "
        r"master=0xf proto=0x20 mute=1 volume=1"
    )
    dut.expect_exact("AUDIO_UNIT_COUNT 2")

    # UAC2 answers MIN/MAX/RES in one RANGE response. The host must read back the
    # range the device actually holds.
    dut.write("v")
    dut.expect_exact("AUDIO_VOLUME_RANGE ok=1 min=-23040 max=0 res=256")
    state = _device_state(device)
    assert state["proto"] == "uac2", state["line"]
    assert state["rate"] == "48000", state["line"]
    assert state["range"] == VOLUME_RANGE, state["line"]


def test_usb_audio_uac2_control_state_round_trip(dut, peers):
    """What the host writes is what the device applies.

    The host repo's copy checks its own read-back, which a device could satisfy by
    echoing. These assertions are on the device's own control state and on the
    events it raised, for the master channel and for logical channel 1 - two
    different entries of the UAC2 Feature Unit's bmaControls array.
    """
    device = peers["device"]
    _sync(dut, device)

    dut.write("w")
    dut.expect_exact("AUDIO_VOLUME set=1 get=1 volume=-1536")
    device.expect(r"DEV_VOL ch=0 db=-1536 n=[1-9]\d*")

    dut.write("c")
    dut.expect_exact(
        f"CHANNEL_CONTROL caps=1 set=1 get=1 mute=1 volume=-3072 range={VOLUME_RANGE}"
    )
    device.expect(r"DEV_MUTE ch=1 m=1 n=[1-9]\d*")
    device.expect(r"DEV_VOL ch=1 db=-3072 n=[1-9]\d*")

    state = _device_state(device)
    assert state["master_vol"] == "-1536", state["line"]
    assert state["ch1_mute"] == "1", state["line"]
    assert state["ch1_vol"] == "-3072", state["line"]
    # The host addressed "the first Feature Unit"; that has to be the playback
    # one, or the assertions above would have been about the microphone.
    assert state["cap_mute"] == "0", state["line"]
    assert state["cap_vol"] == "0", state["line"]

    # Master mute, then unmute, so the streaming test below runs unmuted.
    dut.write("M")
    dut.expect_exact("AUDIO_MUTE set=1 get=1 muted=1")
    device.expect(r"DEV_MUTE ch=0 m=1 n=[1-9]\d*")
    state = _device_state(device)
    assert state["master_mute"] == "1", state["line"]

    dut.write("U")
    dut.expect_exact("AUDIO_UNMUTE clear=1 get=1 muted=0")
    device.expect(r"DEV_MUTE ch=0 m=0 n=[1-9]\d*")
    state = _device_state(device)
    assert state["master_mute"] == "0", state["line"]


def test_usb_audio_uac2_clock_source_request(dut, peers):
    """The sample rate is set on the Clock Source entity, not on the endpoint.

    A UAC1 host writes the rate to the streaming endpoint; a UAC2 host writes it
    to the clock entity resolved through bTerminalLink. The device must accept the
    entity form - a request aimed at an entity it does not have, or at the wrong
    selector, stalls and makes this return 0.

    The device declares one rate, so this sets the rate it already has: the
    assertion is that the request is accepted and the rate still reads back, not
    that it changed (the device only raises SampleRateChanged on a real change).
    """
    device = peers["device"]
    _sync(dut, device)

    dut.write("R")
    dut.expect_exact("AUDIO_RATE_SET 1")

    state = _device_state(device)
    assert state["rate"] == "48000", state["line"]


def test_usb_audio_uac2_streaming_both_directions(dut, peers):
    """Both isochronous directions carry PCM over UAC2.

    This is the streaming validation that was deferred while EspUsbHost was
    UAC1-only. It also checks the asynchronous playback interface's explicit
    feedback endpoint: the device computes the rate from its own FIFO level, and
    the host must be pacing its OUT packets from what it reports.
    """
    device = peers["device"]
    _sync(dut, device)

    dut.write("a")
    dut.expect_exact("AUDIO_OUT_START 1")
    device.expect_exact("AUDIO_INTERFACE SPK 1 alt=1")

    dut.write("I")
    dut.expect_exact("AUDIO_IN_START 1")
    device.expect_exact("AUDIO_INTERFACE MIC 1 alt=1")

    dut.write("r")
    dut.expect_exact("AUDIO_RESET")
    device.write("r")
    device.expect_exact("UAC2_RESET")

    # Host -> device.
    dut.write("s")
    dut.expect("AUDIO_TX [1-9][0-9]*")
    device.expect("DEVICE_RX_AUDIO [1-9][0-9]*")

    # Device -> host: the peer streams a sawtooth continuously while its capture
    # interface is enabled, so the host must see non-silent PCM.
    time.sleep(0.5)
    dut.write("?")
    dut.expect("HOST_RX bytes=[1-9][0-9]* maxAbs=[1-9][0-9]*")

    # Both directions moved data as far as the device is concerned, too.
    device.write("?")
    device.expect("UAC2_ALIVE rx=[1-9][0-9]* tx=[1-9][0-9]* usb_rx=[1-9][0-9]* usb_tx=[1-9][0-9]*")

    # The feedback endpoint is being polled and is pacing the host. The device
    # tracks a 48 kHz clock, so it has no reason to ask for a large correction.
    dut.write("f")
    feedback = dut.expect(
        r"AUDIO_FEEDBACK has=1 rate=(\d+) updates=([1-9]\d*) rejects=(\d+) pacing=(\d+)"
    )
    rate = int(feedback.group(1))
    updates = int(feedback.group(2))
    rejects = int(feedback.group(3))
    pacing = int(feedback.group(4))
    assert 45600 <= rate <= 50400, f"feedback rate {rate} is not near 48000"
    assert pacing == rate, f"pacing rate {pacing} does not follow feedback rate {rate}"
    # Values sent before the device's FIFO is primed fall outside the host's
    # +/-12.5% window and are ignored; those are the only rejects expected.
    assert rejects * 10 < updates, f"{rejects} rejected feedback packets out of {updates}"
