"""HID keyboard + UAC1 speaker in one device, across two boards.

One test rather than three. The first of the three read the host's connect
banner - `HOST_CONNECTED` and the `AUDIO_STREAM` descriptor lines - which is
printed once, when the device is enumerated. A test that is not first never sees
it, so the module only worked in its original order.

Both sketches now answer rather than announce: the device blocks until
`device.ready()` (tud_mounted, so the host has completed SET_CONFIGURATION) and
the host blocks until `onDeviceConnected` has latched an address, each at the top
of its command handler. The descriptor report the audio case needs is a command
now ('S') as well as a connect-time announcement, so it can be asked for at any
point.

The cases are named functions driven from a list, so a failure names the function
it happened in. Each establishes what it needs, so the order is not load-bearing.
"""


def _enumeration(dut, device):
    """Both classes present and claimed, no duplicate endpoint address."""
    dut.write("e")
    dut.expect(
        r"HOST_ENUM pid=4027 ifcount=\d+ eps=\d+ dup=0 "
        r"hid=[1-9]\d* audio=[1-9]\d* claimok=1"
    )


def _audio_descriptor(dut, device):
    """The streaming interface the device advertises, read from its descriptors.

    alt=1 is the active setting, OUT is host-to-device, and maxPacket=98 is one
    48 kHz frame of 16-bit mono plus the extra sample a UAC1 device has to be
    able to absorb. This is descriptor content, so it is the same before and
    after playback runs.
    """
    dut.write("S")
    dut.expect(
        r"AUDIO_STREAM iface=\d+ alt=1 ep=0x02 dir=OUT "
        r"channels=1 bytes=2 bits=16 rate=48000 maxPacket=98"
    )


def _keyboard(dut, device):
    device.write("k")
    device.expect_exact("DEVICE_KEY 1")
    dut.expect_exact("KEY a")


def _playback(dut, device):
    # audioOutputReady() polls for up to 15 s on the host side, which is why this
    # one gets a longer timeout than the rest.
    dut.write("i")
    dut.expect(r"HOST_AUDIO addr=[1-9]\d* ready=1", timeout=20)
    dut.write("a")
    dut.expect_exact("AUDIO_START 1")
    device.expect_exact("AUDIO_INTERFACE PLAYBACK 1 alt=1")

    # Reset first so the byte count that follows was produced by this send and
    # not by a previous run of the case.
    device.write("r")
    device.expect_exact("DEVICE_AUDIO_RESET")
    dut.write("s")
    dut.expect_exact("AUDIO_TX 1")
    device.expect(r"DEVICE_RX_AUDIO [1-9]\d*")


def test_composite_hid_audio(dut, peers):
    device = peers["device"]

    # begin() status over UART, independent of USB enumeration: this says both
    # classes registered without hitting the MAX_CLASSES guard.
    device.write("b")
    device.expect_exact("DEVICE_BEGIN ok ESP_OK")

    # The precondition, asked rather than awaited. The device answers only once
    # the host has configured it, so this both waits and asserts.
    device.write("?")
    device.expect_exact("DEVICE_READY 1")

    for check in (_enumeration, _audio_descriptor, _keyboard, _playback):
        check(dut, device)
