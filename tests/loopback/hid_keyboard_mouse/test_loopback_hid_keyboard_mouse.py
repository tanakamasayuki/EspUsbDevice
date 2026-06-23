def test_loopback_hid_keyboard_mouse(dut):
    dut.expect_exact("HOST_DEVICE")
    dut.expect_exact("KEY k")
    dut.expect_exact("MOUSE x=40 y=0 wheel=0 buttons=0 previous=0 moved=1 changed=0")
    dut.expect_exact("buttons=1 previous=0 moved=0 changed=1")
    dut.expect_exact("buttons=0 previous=1 moved=0 changed=1")
    dut.expect_exact("TEST_END ok")
    assert dut.expect_exact(["OK", "NG"]) == b"OK"
