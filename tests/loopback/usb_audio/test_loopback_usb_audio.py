import pytest


# Known limitation on one-board P4: the loopback link is Full Speed (single UTMI
# PHY held by the device, host limited to the FS port), but the device emits a
# UAC2 descriptor because TUD_OPT_HIGH_SPEED is a compile-time macro (1 on P4),
# while EspUsbHost's audio parser only understands UAC1. So audioOutputStart()
# fails and no PCM reaches the device. Fix is either UAC1-at-FS on the device or
# a UAC2 parser in EspUsbHost. See docs/DESIGN_NOTES.ja.md
# "P4 USB ポート/PHY の実測整理".
@pytest.mark.xfail(
    reason="P4 one-board loopback runs FS but the device emits UAC2; EspUsbHost parses only UAC1",
    strict=False,
)
def test_loopback_usb_audio(dut):
    dut.expect_exact("TEST_BEGIN loopback_usb_audio")
    dut.expect_exact("HOST_READY fs")
    dut.expect_exact("DEVICE_READY fs")
    dut.expect_exact("HOST_DEVICE")
    dut.expect("AUDIO_OUT_READY addr=[0-9]+")
    # P4 runs the high-speed / UAC2 descriptor path, so the exact format fields
    # differ from the UAC1 peer test. Assert that an OUT stream is discovered and
    # that PCM actually reaches the device rather than the exact format numbers.
    dut.expect("AUDIO_STREAM iface=[0-9]+ alt=[1-9][0-9]* ep=0x[0-9a-f]+ dir=OUT")
    dut.expect_exact("AUDIO_START 1")
    dut.expect_exact("AUDIO_INTERFACE SPK 1")
    dut.expect_exact("AUDIO_RESET")
    dut.expect("AUDIO_TX [1-9][0-9]*")
    dut.expect("DEVICE_RX_AUDIO [1-9][0-9]*")
    dut.expect_exact("TEST_END ok")
    assert dut.expect_exact(["OK", "NG"]) == b"OK"
