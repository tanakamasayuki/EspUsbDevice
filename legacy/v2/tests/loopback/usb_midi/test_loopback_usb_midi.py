def expect_both(dut, first_message, second_message):
    first = dut.expect_exact([first_message, second_message])
    if first == first_message.encode():
        dut.expect_exact(second_message)
    else:
        dut.expect_exact(first_message)


def test_loopback_usb_midi(dut):
    dut.expect_exact("HOST_DEVICE")
    expect_both(
        dut,
        "DEVICE_TX_NOTE_ON 1",
        "MIDI_RX cable=0 cin=09 status=90 data1=64 data2=110",
    )

    dut.expect_exact("MIDI_TX_NOTE_ON 1")
    dut.expect_exact("DEVICE_RX cin=09 status=90 data1=60 data2=100")
    dut.expect_exact("MIDI_TX_PROGRAM 1")
    dut.expect_exact("DEVICE_RX cin=0c status=c0 data1=10 data2=0")
    dut.expect_exact("MIDI_TX_BEND 1")
    dut.expect_exact("DEVICE_RX cin=0e status=e0 data1=0 data2=72")
    dut.expect_exact("MIDI_TX_PRESSURE 1")
    dut.expect_exact("DEVICE_RX cin=0d status=d0 data1=77 data2=0")
    dut.expect_exact("MIDI_TX_POLY_PRESSURE 1")
    dut.expect_exact("DEVICE_RX cin=0a status=a0 data1=60 data2=80")
    dut.expect_exact("MIDI_TX_CC 1")
    dut.expect_exact("DEVICE_RX cin=0b status=b0 data1=74 data2=64")

    expect_both(dut, "DEVICE_TX_PROGRAM 1", "MIDI_RX cable=0 cin=0c status=c0 data1=10 data2=0")
    expect_both(dut, "DEVICE_TX_BEND 1", "MIDI_RX cable=0 cin=0e status=e0 data1=0 data2=72")
    expect_both(dut, "DEVICE_TX_PRESSURE 1", "MIDI_RX cable=0 cin=0d status=d0 data1=77 data2=0")
    expect_both(dut, "DEVICE_TX_POLY_PRESSURE 1", "MIDI_RX cable=0 cin=0a status=a0 data1=60 data2=80")
    expect_both(dut, "DEVICE_TX_CC 1", "MIDI_RX cable=0 cin=0b status=b0 data1=74 data2=64")

    dut.expect_exact("MIDI_TX_SYSEX 1")
    dut.expect_exact("DEVICE_RX cin=04 status=f0 data1=125 data2=1")
    dut.expect_exact("DEVICE_RX cin=06 status=02 data1=247 data2=0")
    dut.expect_exact("TEST_END ok")
    assert dut.expect_exact(["OK", "NG"]) == b"OK"
