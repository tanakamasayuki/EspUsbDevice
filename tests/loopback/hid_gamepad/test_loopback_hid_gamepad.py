def test_loopback_hid_gamepad(dut):
    dut.expect_exact("HOST_DEVICE")
    dut.expect_exact("HID_DESC iface=0")
    dut.expect_exact("HID_INPUT iface=0 subclass=0 protocol=0 len=12 data=03 0a f6 14 ec 1e e2 03 05 00 00 00")
    dut.expect("GAMEPAD report=0a f6 14 ec 1e e2 03 05 00 00 00 fields=[1-9][0-9]* changed=1")
    dut.expect("GAMEPAD report=00 00 00 00 00 00 00 00 00 00 00 fields=[1-9][0-9]* changed=1")
    dut.expect("GAMEPAD report=00 00 00 00 00 00 01 00 00 00 00 fields=[1-9][0-9]* changed=1")
    dut.expect("GAMEPAD report=00 00 00 00 00 00 00 ff 7f 00 00 fields=[1-9][0-9]* changed=1")
    dut.expect_exact("TEST_END ok")
    assert dut.expect_exact(["OK", "NG"]) == b"OK"
