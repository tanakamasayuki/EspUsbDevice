"""Multi-cable USB MIDI peer test.

DUT = the USB host (EspUsbHost, ``usb_midi_cables.ino``); the peer = a 4-cable
EspUsbDevice MIDI device (``peer_device/``). tests/peer/usb_midi keeps covering the
default single-cable device.

Two things are checked that the loopback test cannot do on its own:

* ``MIDI_PORT_INFO`` reports the cable counts the host decoded from the peer's
  descriptors, so this is where "the device really advertised four ports" is
  asserted. The cable number in a received message is read out of the packet
  header, so it would echo back correctly even from a one-cable descriptor.
* The two directions are driven from separate boards, so a cable number has to
  survive a real bus rather than a loopback inside one chip.

``getMidiPortInfo()`` is not in a released EspUsbHost yet, so the DUT builds only
from the local checkout: run with ``--profile s3_peer_local``.
"""

CABLE_COUNT = 4


def test_usb_midi_cables_enumerated(dut, peers):
    """The host must see four cables in each direction.

    A wrong cable count here still enumerates and still passes traffic - it just
    shows the wrong number of MIDI ports - which is why it is asserted explicitly.
    """
    device = peers["device"]

    dut.expect_exact("HOST_CONNECTED vid=303a pid=4017")
    device.write("?")
    # 34 head + 30 per cable + 17 per endpoint descriptor, twice.
    device.expect_exact(f"DEVICE_READY cables={CABLE_COUNT} bytes=188")

    dut.write("i")
    m = dut.expect(r"MIDI_PORT_INFO ok=(\d) in=(\d+) out=(\d+)", timeout=10)
    assert int(m.group(1)) == 1, m.group(0)
    assert int(m.group(2)) == CABLE_COUNT, m.group(0)
    assert int(m.group(3)) == CABLE_COUNT, m.group(0)


def test_usb_midi_cables_device_to_host(dut, peers):
    device = peers["device"]

    for cable in range(CABLE_COUNT):
        device.write(str(cable))
        device.expect_exact(f"DEVICE_TX_CABLE {cable} 1")
        dut.expect_exact(
            f"MIDI_RX cable={cable} cin=09 status=90 data1={60 + cable} data2=100"
        )


def test_usb_midi_cables_host_to_device(dut, peers):
    device = peers["device"]

    for cable in range(CABLE_COUNT):
        dut.write(str(cable))
        dut.expect_exact(f"MIDI_TX_CABLE {cable} 1")
        device.expect_exact(
            f"DEVICE_RX cable={cable} cin=09 status=90 data1={70 + cable} data2=90"
        )


def test_usb_midi_unknown_cable_is_refused(dut, peers):
    """Sending on a cable the host was never told about must fail, not silently
    land on another port."""
    device = peers["device"]

    device.write("x")
    device.expect_exact("DEVICE_TX_UNKNOWN_CABLE 0")
