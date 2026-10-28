"""Multi-cable USB MIDI loopback.

tests/loopback/usb_midi keeps covering the default single-cable device. This test
covers a 4-cable one, and specifically that the cable number in a packet header
survives the round trip in both directions - the descriptor can be right while the
cable nibble is dropped or overwritten somewhere in the path, and every message
would still arrive, just on the wrong port.

What this cannot show is that the host actually enumerated four cables: the cable
number reported by ``onMidiMessage`` is decoded straight from the packet header, so
it echoes whatever was sent regardless of what the descriptor advertised. The
descriptor is covered byte by byte in tests/unit/midi_descriptor instead. Once
EspUsbHost releases its cable discovery (``getMidiPortInfo()``), the enumerated
count can be asserted here too.
"""

CABLE_COUNT = 4


def expect_both(dut, first_message, second_message):
    first = dut.expect_exact([first_message, second_message])
    if first == first_message.encode():
        dut.expect_exact(second_message)
    else:
        dut.expect_exact(first_message)


def test_loopback_usb_midi_cables(dut):
    # 34 head + 30 per cable + 17 per endpoint descriptor, twice. The counts are
    # printed per direction because they need not agree; this device is symmetric.
    dut.expect_exact(
        f"DEVICE_READY fs cables={CABLE_COUNT}/{CABLE_COUNT} bytes=188"
    )
    dut.expect_exact("HOST_DEVICE")

    # Device to host, one note per cable. The note number tracks the cable so a
    # packet arriving on the wrong cable cannot be mistaken for a pass.
    for cable in range(CABLE_COUNT):
        expect_both(
            dut,
            f"DEVICE_TX_CABLE {cable} 1",
            f"MIDI_RX cable={cable} cin=09 status=90 data1={60 + cable} data2=100",
        )

    # Cable 4 does not exist on a 4-cable device, so the helper refuses instead of
    # emitting a packet that would land on another port.
    dut.expect_exact("DEVICE_TX_UNKNOWN_CABLE 0")

    # Host to device, raw packets carrying the cable number in the header nibble.
    for cable in range(CABLE_COUNT):
        dut.expect_exact(f"MIDI_TX_CABLE {cable} 1")
        dut.expect_exact(
            f"DEVICE_RX cable={cable} cin=09 status=90 data1={70 + cable} data2=90"
        )

    dut.expect_exact("TEST_END ok")
    assert dut.expect_exact(["OK", "NG"]) == b"OK"
