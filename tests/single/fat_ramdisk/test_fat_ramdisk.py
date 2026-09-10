def test_fat_ramdisk(dut):
    dut.expect_exact("TEST_BEGIN fat_ramdisk")
    dut.expect_exact("TEST_END")
    assert dut.expect_exact(["OK", "NG"]) == b"OK"
