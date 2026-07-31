"""USB MIDI peer test, EspUsbDevice-repo copy.

DUT = the USB host (EspUsbHost, ``usb_midi.ino``); the peer = the EspUsbDevice
MIDI device (``peer_device/``).

``test_usb_midi_device_is_supported`` was added with the move to EspUsbHost
2.7.0: recognising a MIDI streaming interface in ``EspUsbHostDeviceInfo::supported``
arrived in 2.6.0, so with the previously pinned 2.5.2 a MIDI-only device could not
be asserted to enumerate as supported.
"""

def test_usb_midi_device_to_host(dut, peers):
    device = peers["device"]

    dut.expect_exact("HOST_CONNECTED vid=303a pid=4017")
    device.write("?")
    device.expect_exact("DEVICE_READY")

    device.write("n")
    device.expect_exact("DEVICE_TX_NOTE_ON")
    dut.expect_exact("MIDI_RX cable=0 cin=09 status=90 data1=64 data2=110")


def test_usb_midi_host_to_device(dut, peers):
    device = peers["device"]

    dut.write("n")
    dut.expect_exact("MIDI_TX_NOTE_ON 1")
    device.expect_exact("DEVICE_RX cin=09 status=90 data1=60 data2=100")


def test_usb_midi_channel_messages_device_to_host(dut, peers):
    device = peers["device"]

    device.write("p")
    device.expect_exact("DEVICE_TX_PROGRAM")
    dut.expect_exact("MIDI_RX cable=0 cin=0c status=c0 data1=10 data2=0")

    device.write("b")
    device.expect_exact("DEVICE_TX_BEND")
    dut.expect_exact("MIDI_RX cable=0 cin=0e status=e0 data1=0 data2=72")

    device.write("a")
    device.expect_exact("DEVICE_TX_PRESSURE")
    dut.expect_exact("MIDI_RX cable=0 cin=0d status=d0 data1=77 data2=0")

    device.write("y")
    device.expect_exact("DEVICE_TX_POLY_PRESSURE")
    dut.expect_exact("MIDI_RX cable=0 cin=0a status=a0 data1=60 data2=80")

    device.write("c")
    device.expect_exact("DEVICE_TX_CC")
    dut.expect_exact("MIDI_RX cable=0 cin=0b status=b0 data1=74 data2=64")


def test_usb_midi_channel_messages_host_to_device(dut, peers):
    device = peers["device"]

    dut.write("p")
    dut.expect_exact("MIDI_TX_PROGRAM 1")
    device.expect_exact("DEVICE_RX cin=0c status=c0 data1=10 data2=0")

    dut.write("b")
    dut.expect_exact("MIDI_TX_BEND 1")
    device.expect_exact("DEVICE_RX cin=0e status=e0 data1=0 data2=72")

    dut.write("a")
    dut.expect_exact("MIDI_TX_PRESSURE 1")
    device.expect_exact("DEVICE_RX cin=0d status=d0 data1=77 data2=0")

    dut.write("y")
    dut.expect_exact("MIDI_TX_POLY_PRESSURE 1")
    device.expect_exact("DEVICE_RX cin=0a status=a0 data1=60 data2=80")

    dut.write("c")
    dut.expect_exact("MIDI_TX_CC 1")
    device.expect_exact("DEVICE_RX cin=0b status=b0 data1=74 data2=64")


def test_usb_midi_sysex_host_to_device(dut, peers):
    device = peers["device"]

    dut.write("s")
    dut.expect_exact("MIDI_TX_SYSEX 1")
    device.expect_exact("DEVICE_RX cin=04 status=f0 data1=125 data2=1")
    device.expect_exact("DEVICE_RX cin=06 status=02 data1=247 data2=0")


def test_usb_midi_device_is_supported(dut, peers):
    """A MIDI-only device must enumerate as supported, with its two interfaces.

    EspUsbDeviceMidi publishes an Audio Control + MIDI Streaming pair and nothing
    else - no HID, CDC, MSC or vendor interface. Until EspUsbHost 2.6.0 counted a
    MIDI interface, `supported` was false for exactly this device, so a sketch
    gating on that flag would have ignored it.
    """
    device = peers["device"]
    device.write("?")
    device.expect_exact("DEVICE_READY")

    # Re-read the connect line from the boot banner rather than replugging.
    dut.write("i")
    m = dut.expect(r"DEVICE_INFO vid=303a pid=4017 supported=(\d) interfaces=(\d+)", timeout=10)
    assert int(m.group(1)) == 1, m.group(0)
    # AudioControl + MIDIStreaming.
    assert int(m.group(2)) == 2, m.group(0)
