"""Runtime PCM buffers and real isochronous transfers on the S3 pair."""


def test_audio_stream(dut, peers):
    device = peers["device"]
    try:
        for protocol, directions, rate, frames, external in (
            (2, 3, 48000, 256, 0),
            (1, 3, 44100, 256, 1),
            (2, 1, 48000, 1, 1),
            (2, 2, 44100, 0, 0),
        ):
            device.write(f"start {protocol} {directions} {rate} {frames} {external}")
            device.expect_exact("STARTED 1\r\n", timeout=30)
            dut.write("start")
            dut.expect_exact("HOST_READY 1\r\n", timeout=30)
            device.write("locked")
            device.expect_exact("FORMATS_LOCKED 1\r\n", timeout=5)
            for attempt in range(2):
                dut.write(f"run {directions} {rate}")
                r = dut.expect(r"PCM ok=1 bytes=(\d+) bad=0 filled=(\d+) feedback=(\d+) rejects=0\r\n", timeout=20)
                if directions & 2:
                    assert int(r.group(1)) > rate * 4 * 1.3
                    assert bool(int(r.group(2))) == bool(frames)
                if protocol == 2 and directions & 1:
                    assert int(r.group(3)) > 0
                device.write("stats")
                r = device.expect(r"STATS received=(\d+) bad=0 rx=(\d+) tx=(\d+) underrun=(\d+)\r\n", timeout=5)
                if directions & 1:
                    assert int(r.group(2)) > rate * 4 * 1.3
                    assert int(r.group(1)) > 0
                if directions & 2:
                    assert int(r.group(3)) > 10000
                    if not frames:
                        assert int(r.group(4)) > 10000
                if attempt == 0:
                    dut.write("suspend")
                    dut.expect_exact("ISO_SUSPENDED 1\r\n", timeout=10)
                    device.write("suspended")
                    device.expect_exact("ISO_STATE_SUSPENDED 1\r\n", timeout=5)
                    dut.write("resume")
                    r = dut.expect(r"PCM_RESUMED ok=1 bytes=(\d+) bad=0 filled=(\d+) feedback=(\d+) rejects=0\r\n", timeout=20)
                    if directions & 2:
                        assert int(r.group(1)) > rate * 4 * 1.3
                        assert bool(int(r.group(2))) == bool(frames)
                    if protocol == 2 and directions & 1:
                        assert int(r.group(3)) > 0
                    device.write("resumed")
                    device.expect(r"ISO_STATE_RESUMED ok=1 rx=\d+ tx=\d+ bad=0\r\n", timeout=5)
                dut.write("pause")
                dut.expect_exact("PAUSED 1\r\n", timeout=10)
            if directions & 2 and frames:
                device.write("silence")
                device.expect_exact("SILENCE\r\n", timeout=5)
                dut.write(f"run {directions} {rate}")
                dut.expect(r"PCM ok=1 bytes=\d+ bad=0 filled=0 feedback=\d+ rejects=0\r\n", timeout=20)
                device.write("stats")
                r = device.expect(r"STATS received=\d+ bad=0 rx=\d+ tx=\d+ underrun=(\d+)\r\n", timeout=5)
                assert int(r.group(1)) > rate * 4 * 1.3
                dut.write("pause")
                dut.expect_exact("PAUSED 1\r\n", timeout=10)
            for board in (dut, device):
                board.write("stop")
                board.expect_exact("STOPPED\r\n", timeout=30)
    finally:
        errors = []
        for name, board in (("host", dut), ("device", device)):
            try:
                board.write("stop")
                board.expect_exact("STOPPED\r\n", timeout=30)
            except Exception as exc:
                errors.append(f"{name}: {exc}")
        assert not errors, "USB stop failed: " + "; ".join(errors)


def test_audio_reconfigure(dut, peers):
    """The same Function and stream references change rate after end()."""
    device = peers["device"]
    try:
        for protocol in (2, 1):
            old_rate = 0
            for rate in (48000, 44100, 48000):
                device.write(f"start {protocol} 3 {rate} 64 0")
                if old_rate:
                    device.expect_exact(f"RECONFIGURED 1 from={old_rate} to={rate}\r\n", timeout=10)
                device.expect_exact("STARTED 1\r\n", timeout=30)
                dut.write("start")
                dut.expect_exact("HOST_READY 1\r\n", timeout=30)
                device.write("locked")
                device.expect_exact("FORMATS_LOCKED 1\r\n", timeout=5)
                dut.write(f"run 3 {rate}")
                r = dut.expect(r"PCM ok=1 bytes=(\d+) bad=0 filled=(\d+) feedback=(\d+) rejects=0\r\n", timeout=20)
                assert int(r.group(1)) > rate * 4 * 1.3
                assert int(r.group(2)) > 0
                if protocol == 2:
                    assert int(r.group(3)) > 0
                device.write("stats")
                r = device.expect(r"STATS received=(\d+) bad=0 rx=(\d+) tx=(\d+) underrun=\d+\r\n", timeout=5)
                assert int(r.group(1)) > 0 and int(r.group(2)) > rate * 4 * 1.3 and int(r.group(3)) > rate * 4 * 1.3
                for board in (dut, device):
                    board.write("stop")
                    board.expect_exact("STOPPED\r\n", timeout=30)
                old_rate = rate
    finally:
        errors = []
        for name, board in (("host", dut), ("device", device)):
            try:
                board.write("stop")
                board.expect_exact("STOPPED\r\n", timeout=30)
            except Exception as exc:
                errors.append(f"{name}: {exc}")
        assert not errors, "USB stop failed: " + "; ".join(errors)


def test_audio_sample_rates(dut, peers):
    """Thirteen runtime rates, long UAC2 RANGE, rate changes without rebuilding."""
    device = peers["device"]
    try:
        for protocol in (2, 1):
            device.write(f"start {protocol} 3 48000 64 0 multi")
            device.expect_exact("STARTED 1\r\n", timeout=30)
            dut.write("start")
            dut.expect_exact("HOST_READY 1\r\n", timeout=30)
            if protocol == 2:
                dut.write("ranges")
                dut.expect_exact("RANGES 1 bytes=158 checks=10\r\n", timeout=20)
            dut.write(f"invalid_rate {protocol}")
            dut.expect_exact("UNSUPPORTED_RATE 1\r\n", timeout=5)
            device.write("clock")
            device.expect_exact("CLOCK 48000\r\n", timeout=5)
            for rate in (48000, 44100, 48000):
                device.write("resetstats")
                device.expect_exact("STATS_RESET\r\n", timeout=5)
                dut.write(f"run 3 {rate}")
                r = dut.expect(r"PCM ok=1 bytes=(\d+) bad=0 filled=(\d+) feedback=(\d+) rejects=0\r\n", timeout=20)
                assert int(r.group(1)) > rate * 4 * 1.3 and int(r.group(2)) > 0
                if protocol == 2:
                    assert int(r.group(3)) > 0
                device.write("clock")
                device.expect_exact(f"CLOCK {rate}\r\n", timeout=5)
                device.write("stats")
                r = device.expect(r"STATS received=(\d+) bad=0 rx=(\d+) tx=(\d+) underrun=\d+\r\n", timeout=5)
                assert int(r.group(1)) > 0 and int(r.group(2)) > rate * 4 * 1.3 and int(r.group(3)) > rate * 4 * 1.3
                dut.write("pause")
                dut.expect_exact("PAUSED 1\r\n", timeout=10)
            for board in (dut, device):
                board.write("stop")
                board.expect_exact("STOPPED\r\n", timeout=30)
    finally:
        errors = []
        for name, board in (("host", dut), ("device", device)):
            try:
                board.write("stop")
                board.expect_exact("STOPPED\r\n", timeout=30)
            except Exception as exc:
                errors.append(f"{name}: {exc}")
        assert not errors, "USB stop failed: " + "; ".join(errors)
