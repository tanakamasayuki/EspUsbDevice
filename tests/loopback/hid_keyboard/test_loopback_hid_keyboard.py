# The four LED states are expected in order and the "LED_TX" lines are not.
#
# Both come from the same board - this is the one-board loopback - but from
# different tasks: "LED_TX" is printed by the loop task when setKeyboardLeds()
# returns, and "LED numlock=..." by the device task when the report arrives.
# Nothing orders those two prints against each other, and with EspUsbHost 2.9.1
# the device's print began winning: the last state appeared before its own
# LED_TX. The states stay ordered among themselves because each send waits for
# the previous one to arrive before the next goes out.
#
# Nothing is lost by dropping them. PHASE_NORMAL ok is printed only when all
# four sends returned true and each expected state was received, which is
# strictly more than the LED_TX lines asserted.
def test_loopback_hid_keyboard(dut):
    dut.expect_exact("HOST_DEVICE")
    dut.expect_exact("HID_DESC iface=0")
    dut.expect_exact("HID_INPUT iface=0 subclass=1 protocol=1 len=8 data=00 00 0b")
    dut.expect_exact("hello, keyboard")
    dut.expect_exact("LED numlock=1 capslock=0 scrolllock=0")
    dut.expect_exact("LED numlock=0 capslock=1 scrolllock=0")
    dut.expect_exact("LED numlock=0 capslock=0 scrolllock=1")
    dut.expect_exact("LED numlock=0 capslock=0 scrolllock=0")
    dut.expect_exact("PHASE_NORMAL ok")
    dut.expect_exact("REVERSE_HOST_READY hs")
    dut.expect_exact("REVERSE_DEVICE_READY fs")
    dut.expect_exact("HOST_DEVICE")
    dut.expect_exact("HID_DESC iface=0")
    dut.expect_exact("hello, keyboard")
    dut.expect_exact("LED numlock=0 capslock=1 scrolllock=0")
    dut.expect_exact("PHASE_REVERSE ok")
    dut.expect_exact("TEST_END ok")
    assert dut.expect_exact(["OK", "NG"]) == b"OK"
