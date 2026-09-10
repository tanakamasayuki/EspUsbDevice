"""USB MIDI peer test, EspUsbDevice-repo copy.

DUT = the USB host (EspUsbHost, ``usb_midi.ino``); the peer = the EspUsbDevice
MIDI device (``peer_device/``).

``_device_is_supported`` was added with the move to EspUsbHost 2.7.0: recognising
a MIDI streaming interface in ``EspUsbHostDeviceInfo::supported`` arrived in
2.6.0, so with the previously pinned 2.5.2 a MIDI-only device could not be
asserted to enumerate as supported.

One test rather than six. The first of the six read ``HOST_CONNECTED``, printed
once at connect, and the other five sent MIDI straight away - which works only
while something ahead of them has waited for enumeration. Both sketches answer
rather than announce now; see ``tests/peer/composite_hid_cdc`` for the full note.

The cases are named functions driven from a list. Each message is self-contained
- sent, then observed on the other side - so the order is not load-bearing.
"""


def _device_to_host_note(dut, device):
    device.write("n")
    device.expect_exact("DEVICE_TX_NOTE_ON")
    dut.expect_exact("MIDI_RX cable=0 cin=09 status=90 data1=64 data2=110")


def _host_to_device_note(dut, device):
    dut.write("n")
    dut.expect_exact("MIDI_TX_NOTE_ON 1")
    device.expect_exact("DEVICE_RX cin=09 status=90 data1=60 data2=100")


def _channel_messages_device_to_host(dut, device):
    """Every channel-voice message type, each with its own Code Index Number.

    The CIN is what a receiver uses to size the packet, so a message that
    arrives with the right status byte and the wrong CIN is still broken.
    """
    for command, expected in (
        ("p", "MIDI_RX cable=0 cin=0c status=c0 data1=10 data2=0"),
        ("b", "MIDI_RX cable=0 cin=0e status=e0 data1=0 data2=72"),
        ("a", "MIDI_RX cable=0 cin=0d status=d0 data1=77 data2=0"),
        ("y", "MIDI_RX cable=0 cin=0a status=a0 data1=60 data2=80"),
        ("c", "MIDI_RX cable=0 cin=0b status=b0 data1=74 data2=64"),
    ):
        device.write(command)
        device.expect(r"DEVICE_TX_\w+")
        dut.expect_exact(expected)


def _channel_messages_host_to_device(dut, device):
    for command, sent, received in (
        ("p", "MIDI_TX_PROGRAM 1", "DEVICE_RX cin=0c status=c0 data1=10 data2=0"),
        ("b", "MIDI_TX_BEND 1", "DEVICE_RX cin=0e status=e0 data1=0 data2=72"),
        ("a", "MIDI_TX_PRESSURE 1", "DEVICE_RX cin=0d status=d0 data1=77 data2=0"),
        ("y", "MIDI_TX_POLY_PRESSURE 1", "DEVICE_RX cin=0a status=a0 data1=60 data2=80"),
        ("c", "MIDI_TX_CC 1", "DEVICE_RX cin=0b status=b0 data1=74 data2=64"),
    ):
        dut.write(command)
        dut.expect_exact(sent)
        device.expect_exact(received)


def _sysex_host_to_device(dut, device):
    """A SysEx message split across two packets: CIN 0x4 starts/continues and
    CIN 0x6 ends with two bytes, which is the part a single-packet
    implementation gets wrong."""
    dut.write("s")
    dut.expect_exact("MIDI_TX_SYSEX 1")
    device.expect_exact("DEVICE_RX cin=04 status=f0 data1=125 data2=1")
    device.expect_exact("DEVICE_RX cin=06 status=02 data1=247 data2=0")


def _device_is_supported(dut, device):
    """A MIDI-only device must enumerate as supported, with its two interfaces.

    EspUsbDeviceMidi publishes an Audio Control + MIDI Streaming pair and nothing
    else - no HID, CDC, MSC or vendor interface. Until EspUsbHost 2.6.0 counted a
    MIDI interface, `supported` was false for exactly this device, so a sketch
    gating on that flag would have ignored it.
    """
    dut.write("i")
    m = dut.expect(r"DEVICE_INFO vid=303a pid=4017 supported=(\d) interfaces=(\d+)", timeout=10)
    assert int(m.group(1)) == 1, m.group(0)
    # AudioControl + MIDIStreaming.
    assert int(m.group(2)) == 2, m.group(0)


def test_usb_midi(dut, peers):
    device = peers["device"]

    device.write("?")
    device.expect_exact("DEVICE_READY 1")

    checks = (
        _device_to_host_note,
        _host_to_device_note,
        _channel_messages_device_to_host,
        _channel_messages_host_to_device,
        _sysex_host_to_device,
        _device_is_supported,
    )
    for check in checks:
        check(dut, device)
