def test_rom_dfu_guard(dut):
    dut.expect_exact("TEST_BEGIN rom_dfu_guard")
    # The board is read over USB Serial/JTAG, which burning USB_PHY_SEL would
    # remove, so "unburned" is the only reachable state here. Asserting it keeps
    # a silently skipped run from looking like a pass.
    dut.expect_exact("USB_PHY_SEL=unburned")
    dut.expect_exact("STILL_RUNNING")
    dut.expect_exact("TEST_END")
    assert dut.expect_exact(["OK", "NG"]) == b"OK"
