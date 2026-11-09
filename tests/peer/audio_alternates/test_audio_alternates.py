"""Raw SDK Host claims each PCM alternate and checks real ISO payloads."""


def test_audio_alternates(dut, peers):
    device = peers["device"]

    def command(board, text, reply, timeout=10):
        board.write(text)
        board.expect_exact(reply + "\r\n", timeout=timeout)

    try:
        for external, protocol in ((0, 2), (0, 1), (1, 2), (1, 1)):
            command(device, f"start {protocol} 3 24000 65 {external} alternates", "STARTED 1", 30)
            command(dut, "start", "HOST_READY 1", 30)
            previous = None
            for alternate, bits, bytes_, rate in (
                (1,16,2,24000),(2,24,3,16000),(3,32,4,24000),
                (4,24,4,16000),(3,32,4,16000),(2,24,3,24000),
                (1,16,2,16000),(4,24,4,24000),
            ):
                command(device, "formats_off", "FORMATS_OFF 1")
                if previous is not None:
                    command(device, "poison", "POISONED 1")
                command(device, "resetstats", "STATS_RESET")
                dut.write(f"run {alternate} {rate}")
                r = dut.expect(r"ISO ok=1 rx=(\d+) tx=(\d+) filled=(\d+) feedback=(\d+) bad=0 lost=\d+\r\n", timeout=20)
                frame = 2 * bytes_
                assert int(r.group(1)) >= 50 * frame and int(r.group(2)) >= 50 * frame and int(r.group(3)) > 0
                if protocol == 2:
                    assert int(r.group(4)) > 0
                command(dut, "invalid_alt", "ALTERNATES 1")
                command(device, f"formats {bits} {rate} {bytes_}", f"PCM_FORMAT ok=1 bits={bits} rate={rate} capacity={520 // frame * frame}")
                device.write("iso_diag")
                device.expect(r"ISO_DIAG sts=[0-9a-f]+ mask=[0-9a-f]+ outctl=[0-9a-f]+ outsize=[0-9a-f]+ outint=[0-9a-f]+\r\n", timeout=5)
                device.write("stats")
                r = device.expect(r"STATS received=(\d+) bad=0 rx=(\d+) tx=(\d+) underrun=\d+\r\n", timeout=5)
                assert int(r.group(1)) > 0 and int(r.group(2)) >= 50 * frame and int(r.group(3)) >= 50 * frame
                command(dut, "pause", "PAUSED 1")
                command(device, "guards", f"RING_GUARDS ok=1 external={external} bytes={520 if external else 0}")
                previous = alternate
            for board in (dut, device):
                command(board, "stop", "STOPPED", 30)
            command(device, "guards", f"RING_GUARDS ok=1 external={external} bytes={520 if external else 0}")
    finally:
        errors = []
        for name, board in (("host", dut), ("device", device)):
            try:
                command(board, "stop", "STOPPED", 30)
            except Exception as exc:
                errors.append(f"{name}: {exc}")
        assert not errors, "USB stop failed: " + "; ".join(errors)
