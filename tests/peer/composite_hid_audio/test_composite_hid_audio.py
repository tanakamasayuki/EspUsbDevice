def test_composite_hid_audio_enumerates(dut, peers):
    device = peers["device"]

    dut.expect_exact("HOST_CONNECTED vid=303a pid=4027")
    dut.expect(
        r"AUDIO_STREAM iface=\d+ alt=1 ep=0x02 dir=OUT "
        r"channels=1 bytes=2 bits=16 rate=48000 maxPacket=98"
    )
    device.write("b")
    device.expect_exact("DEVICE_BEGIN ok ESP_OK")

    dut.write("e")
    dut.expect(
        r"HOST_ENUM pid=4027 ifcount=\d+ eps=\d+ dup=0 "
        r"hid=[1-9]\d* audio=[1-9]\d* claimok=1"
    )


def test_composite_hid_audio_keyboard_works(dut, peers):
    device = peers["device"]

    device.write("k")
    device.expect_exact("DEVICE_KEY 1")
    dut.expect_exact("KEY a")


def test_composite_hid_audio_playback_works(dut, peers):
    device = peers["device"]

    dut.write("i")
    dut.expect(r"HOST_AUDIO addr=[1-9]\d* ready=1", timeout=20)
    dut.write("a")
    dut.expect_exact("AUDIO_START 1")
    device.expect_exact("AUDIO_INTERFACE PLAYBACK 1 alt=1")

    device.write("r")
    device.expect_exact("DEVICE_AUDIO_RESET")
    dut.write("s")
    dut.expect_exact("AUDIO_TX 1")
    device.expect(r"DEVICE_RX_AUDIO [1-9]\d*")
