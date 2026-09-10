"""Multi-cable USB MIDI peer test.

DUT = the USB host (EspUsbHost, ``usb_midi_cables.ino``); the peer = an asymmetric
EspUsbDevice MIDI device with 4 cables device-to-host and 5 host-to-device
(``peer_device/``). tests/peer/usb_midi keeps covering the default single-cable
device, tests/loopback/usb_midi_cables the symmetric multi-cable case, and
tests/unit/midi_descriptor the descriptor bytes for every combination of counts.

The counts differ on purpose. The MIDI class names embedded jacks from the device's
side, the opposite of the endpoint direction they belong to, so a Host that swaps
the two directions is invisible to a symmetric device - and to any round trip, since
a received packet's cable number comes from its own header. 4 in / 5 out is the only
way this rig can tell ``inCableCount`` from ``outCableCount``.

Not sixteen cables: the ESP-IDF USB Host refuses a configuration descriptor longer
than its enumeration control transfer ("Configuration descriptor larger than control
transfer max length"), and CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE is 256 in the
precompiled Arduino libraries with no way to raise it from a sketch. This 4/5 device
is 204 bytes of MIDI descriptor plus the 9-byte configuration header = 213 and
enumerates; a symmetric 6-cable device is 261 and does not enumerate at all, so
nothing downstream of it can be tested. The 16-cable device the library can build is
legal USB and a PC host takes it - it is this Host stack that cannot.

What only this rig can check:

* ``getMidiPortInfo()`` reports the cable counts EspUsbHost decoded from the peer's
  descriptors, so this is where "the device really advertised these ports, in these
  directions" is asserted. The cable number in a received message is read out of the
  packet header, so it would come back correct even from a one-cable descriptor.
* The count is asked for before any MIDI traffic and again afterwards. Coming from
  the descriptors rather than from observed traffic is the whole point of the
  feature for EspMidi, whose ports are allocated at connect time - a silent cable
  that never sends must still be counted.
* Cable numbers are decoded per packet, not per transfer. Several packets on
  different cables inside one bulk transfer is the case a per-transfer decode gets
  wrong, and it cannot occur without more than one cable.
* Which direction is which, from the asymmetry. A swap reports 5 in / 4 out.

``getMidiPortInfo()`` is not in a released EspUsbHost yet, so the DUT builds only
from the local checkout: run with ``--profile s3_peer_local``.

Not covered here, and why:

* **A CS_ENDPOINT belonging to a non-MIDI endpoint.** The host latches the
  direction of the MIDI bulk endpoint it just passed and clears it at every
  endpoint descriptor. Audio isochronous endpoints also carry a CS_ENDPOINT, so a
  composite Audio + MIDI device is what would show a leaking latch; that needs a
  composite peer sketch of its own.
* **Two MIDI Streaming interfaces**, the case the claim gate was tightened for.
  `EspUsbDeviceMidi` allows one instance per device, so this peer cannot produce it.

One test rather than ten, and here the merge does more than remove a boot-banner
dependency: this module is a sequence. ``_cable_count_before_any_traffic`` is only
that test if nothing has sent MIDI yet, and ``_cable_count_unchanged_after_traffic``
is only that test if everything in between has run. As ten separate tests that
held by luck of collection order; as one ordered list it is stated. So this module
is deliberately not a candidate for a reversed run.
"""

IN_CABLES = 4
OUT_CABLES = 5

# Cables visited out of order in both sketches, so a cable decoded once per
# transfer instead of once per packet shows up as the wrong pairing.
INTERLEAVED_CABLES = [3, 0, 2, 1]


def _port_info(dut):
    dut.write("i")
    m = dut.expect(r"MIDI_PORT_INFO ok=(\d) in=(\d+) out=(\d+) iface=(\d+)", timeout=10)
    return m


def _cable_count_before_any_traffic(dut, device):
    """The host must report 4 cables device-to-host and 5 host-to-device, from the
    descriptors alone.

    A wrong cable count still enumerates and still passes traffic - it just shows
    the wrong number of MIDI ports - so it is asserted explicitly. This runs before
    any MIDI message is exchanged, because a count that only becomes right after
    traffic arrives is exactly what EspMidi cannot use.
    """
    # The device's own view of what it published, which is what the host below
    # has to agree with.
    # 34 head + 30 per two-way cable + 15 per one-way cable + 17 + 18 endpoints.
    device.write("?")
    device.expect_exact(f"DEVICE_CABLES in={IN_CABLES} out={OUT_CABLES} bytes=204")

    m = _port_info(dut)
    assert int(m.group(1)) == 1, m.group(0)
    # Swapped directions would read 5 / 4 here. Nothing else in this test, or in any
    # round trip, can distinguish that.
    assert int(m.group(2)) == IN_CABLES, m.group(0)
    assert int(m.group(3)) == OUT_CABLES, m.group(0)
    # The reported interface is the MIDI Streaming one, which is interface 1 for a
    # MIDI-only device (AudioControl is 0). Asserted because the counts come from
    # the endpoint descriptors while the interface number comes from the interface
    # descriptor: if the endpoint scan were not restricted to the tracked
    # interface, the two could end up describing different interfaces.
    assert int(m.group(4)) == 1, m.group(0)


def _all_cables_device_to_host(dut, device):
    """Every cable this device sends on."""
    device.write("A")
    for cable in range(IN_CABLES):
        device.expect_exact(f"DEVICE_TX_CABLE {cable} 1")
        dut.expect_exact(
            f"MIDI_RX cable={cable} cin=09 status=90 data1={60 + cable} data2=100"
        )


def _all_cables_host_to_device(dut, device):
    dut.write("A")
    for cable in range(OUT_CABLES):
        dut.expect_exact(f"MIDI_TX_CABLE {cable} 1")
        device.expect_exact(
            f"DEVICE_RX cable={cable} cin=09 status=90 data1={70 + cable} data2=90"
        )


def _interleaved_cables_device_to_host(dut, device):
    """Four packets on four cables, written back to back so they share a transfer.

    The note numbers stay sequential while the cables do not, so a cable applied
    once per transfer rather than once per packet pairs them up wrongly instead of
    just losing a message.
    """
    device.write("I")
    device.expect_exact("DEVICE_TX_INTERLEAVE 1")
    for index, cable in enumerate(INTERLEAVED_CABLES):
        dut.expect_exact(
            f"MIDI_RX cable={cable} cin=09 status=90 data1={100 + index} data2=100"
        )


def _interleaved_cables_host_to_device(dut, device):
    """The same, in the direction where the four packets really are one transfer:
    the host writes them with a single midiSend()."""
    dut.write("I")
    dut.expect_exact("MIDI_TX_INTERLEAVE 1")
    for index, cable in enumerate(INTERLEAVED_CABLES):
        device.expect_exact(
            f"DEVICE_RX cable={cable} cin=09 status=90 data1={110 + index} data2=90"
        )


def _sysex_on_non_zero_cable_device_to_host(dut, device):
    """A SysEx message split across packets keeps its cable on every packet.

    Each packet repeats the cable number, so reassembly is where one can be lost -
    and a message that starts on cable 3 and ends on cable 0 is not a message.
    """
    device.write("S")
    device.expect_exact("DEVICE_TX_SYSEX 1")
    dut.expect_exact("MIDI_RX cable=3 cin=04 status=f0 data1=125 data2=1")
    dut.expect_exact("MIDI_RX cable=3 cin=06 status=02 data1=247 data2=0")


def _sysex_on_non_zero_cable_host_to_device(dut, device):
    dut.write("S")
    dut.expect_exact("MIDI_TX_SYSEX 1")
    device.expect_exact("DEVICE_RX cable=3 cin=04 status=f0 data1=125 data2=1")
    device.expect_exact("DEVICE_RX cable=3 cin=06 status=02 data1=247 data2=0")


def _unknown_cable_is_refused(dut, device):
    """Cable 5 is past this device's cables in both directions, so sending on it must
    fail rather than land on another port."""
    device.write("x")
    device.expect_exact("DEVICE_TX_UNKNOWN_CABLE 0")


def _receive_only_cable_is_refused_for_sending(dut, device):
    """Cable 4 exists for receiving but not for sending, and must be refused anyway.

    The range a sender is bounded by is inCableCount(), not outCableCount() - the
    Host has no port to deliver this to. Only an asymmetric device can show the
    difference; on a symmetric one both bounds are the same number.
    """
    device.write("X")
    device.expect_exact("DEVICE_TX_RECEIVE_ONLY_CABLE 0")


def _cable_count_unchanged_after_traffic(dut, device):
    """The count must be the same as before any traffic.

    Traffic has now been seen on every cable in both directions. If the count were
    accumulated from observed cable numbers rather than read from the descriptors,
    the two readings would differ - and the first one, taken at connect time, is
    the one EspMidi allocates its ports from.
    """
    m = _port_info(dut)
    assert int(m.group(1)) == 1, m.group(0)
    assert int(m.group(2)) == IN_CABLES, m.group(0)
    assert int(m.group(3)) == OUT_CABLES, m.group(0)


def test_usb_midi_cables(dut, peers):
    device = peers["device"]

    device.write("?")
    device.expect_exact("DEVICE_READY 1")
    dut.write("?")
    dut.expect_exact("HOST_READY 1 vid=303a pid=4017")

    # Ordered on purpose: the first and last cases are a before/after pair around
    # everything between them.
    checks = (
        _cable_count_before_any_traffic,
        _all_cables_device_to_host,
        _all_cables_host_to_device,
        _interleaved_cables_device_to_host,
        _interleaved_cables_host_to_device,
        _sysex_on_non_zero_cable_device_to_host,
        _sysex_on_non_zero_cable_host_to_device,
        _unknown_cable_is_refused,
        _receive_only_cable_is_refused_for_sending,
        _cable_count_unchanged_after_traffic,
    )
    for check in checks:
        check(dut, device)
