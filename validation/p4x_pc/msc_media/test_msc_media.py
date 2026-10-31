"""MSC medium change and removal, checked on the wire by a BOT client on the PC."""

import json
import pathlib
import struct
import time

import usb.core
import usb.util

import sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from pc_bot import Bot, rw10  # noqa: E402

SERIAL = "espusb-msc-media"
BLOCK = 512
RESULTS = pathlib.Path(__file__).resolve().parents[2] / ".pytest-results" / "msc-media.json"
TUR = bytes(6)
INQUIRY = bytes([0x12, 0, 0, 0, 36, 0])
SENSE = bytes([0x03, 0, 0, 0, 18, 0])


def checksum(data):
    total = 0
    for b in data:
        total = (total * 31 + b) & 0xFFFFFFFF
    return total


def original(lba):
    return bytes((lba * 7 + lba * BLOCK + i) & 0xFF for i in range(BLOCK))


def find(timeout=10.0):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        for d in usb.core.find(find_all=True, idVendor=0x1209, idProduct=0x0001):
            try:
                if usb.util.get_string(d, d.iSerialNumber) == SERIAL:
                    return d
            except (usb.core.USBError, ValueError):
                pass
        time.sleep(0.01)
    raise AssertionError("MSC device did not enumerate on the PC")


def test_msc_media(dut):
    results = {}

    def device(text, reply):
        dut.write(text)
        dut.expect_exact(reply + "\r\n", timeout=10)

    def present(*values):
        for v in values:
            device(f"present {v}", "PRESENT_SET")

    def counts():
        dut.write("counts")
        m = dut.expect(r"COUNTS reads=(\d+) writes=(\d+) readsAfter=(\d+) writesAfter=(\d+) present=(\d)\r\n", timeout=5)
        return tuple(int(v) for v in m.groups())

    def block(lba):
        dut.write(f"block {lba}")
        return int(dut.expect(rf"BLOCK {lba} sum=(\d+)\r\n", timeout=5).group(1))

    UA, NOT_PRESENT, NONE = (6, 0x28, 0), (2, 0x3A, 0), (0, 0, 0)
    try:
        dut.write("stop")
        dut.expect_exact("STOPPED\r\n", timeout=30)
        device("start", "STARTED 1")
        dev = find()
        assert dev.speed == usb.util.SPEED_HIGH
        time.sleep(1)  # let usb-storage finish its probe before taking the interface over
        bot = Bot(dev)
        # The initial present=true and whatever the kernel sent leave no notification.
        while bot.tur():
            bot.sense()
        assert bot.sense() == NONE

        # Removal and the same value again.
        present(0)
        results["removed"] = [bot.tur(), bot.sense()]
        present(0)
        results["removed_again"] = [bot.tur(), bot.sense()]
        assert results["removed"] == results["removed_again"] == [1, NOT_PRESENT]

        # Insertion: INQUIRY and REQUEST SENSE pass without taking the notification.
        present(1)
        inquiry = bot.command(INQUIRY, 36)
        results["inserted"] = [inquiry["status"], bot.sense(), bot.tur(), bot.sense(), bot.tur()]
        assert results["inserted"] == [0, NONE, 1, UA, 0]
        present(1)
        results["same_true"] = bot.tur()
        assert results["same_true"] == 0

        # Several changes before the Host asks: one notification.
        present(0, 1, 0, 1)
        results["collapsed"] = [bot.tur(), bot.sense(), bot.tur()]
        assert results["collapsed"] == [1, UA, 0]
        # Gone again before it was told: the change first, then absence.
        present(0, 1, 0)
        results["told_then_absent"] = [bot.tur(), bot.sense(), bot.tur(), bot.sense()]
        assert results["told_then_absent"] == [1, UA, 1, NOT_PRESENT]
        present(1)
        assert [bot.tur(), bot.sense(), bot.tur()] == [1, UA, 0]

        # Bulk-only reset recovery keeps an untold change.
        present(0, 1)
        bot.reset_recovery()
        results["after_bot_reset"] = [bot.tur(), bot.sense(), bot.tur()]
        assert results["after_bot_reset"] == [1, UA, 0]
        # It keeps the sense of the last failure as well.
        present(0)
        failed = bot.tur()
        bot.reset_recovery()
        results["sense_after_bot_reset"] = [failed, bot.sense()]
        assert results["sense_after_bot_reset"] == [1, NOT_PRESENT]
        present(1)
        assert [bot.tur(), bot.sense(), bot.tur()] == [1, UA, 0]

        def pulled_read(reinsert):
            device(f"pull 1 {reinsert}", "PULL_ARMED")
            r = bot.command(rw10(0x28, 0, 4), 4 * BLOCK)
            ok_data = r["data"] == original(0)
            return {k: r[k] for k in ("bytes", "stalled", "status", "residue")} | {
                "first_block_ok": ok_data, "counts": counts(), "sense": bot.sense()}

        # READ(10) of 4 blocks, medium pulled inside the first block's callback.
        results["read_pulled"] = r = pulled_read(0)
        assert (r["bytes"], r["stalled"], r["status"], r["residue"], r["first_block_ok"]) == (512, True, 1, 1536, True)
        assert r["counts"][:4] == (1, 0, 0, 0) and r["sense"] == NOT_PRESENT
        present(1)
        assert [bot.tur(), bot.sense(), bot.tur()] == [1, UA, 0]

        # Pulled and put back inside the callback: the old command still never goes on.
        results["read_reinserted"] = r = pulled_read(1)
        assert (r["bytes"], r["stalled"], r["status"], r["residue"]) == (512, True, 1, 1536)
        assert r["counts"] == (1, 0, 0, 0, 1) and r["sense"] == NOT_PRESENT
        results["read_reinserted_next"] = [bot.tur(), bot.sense(), bot.tur()]
        assert results["read_reinserted_next"] == [1, UA, 0]

        # Pulled in the last block's callback: nothing is left to fail.
        device("pull 4 0", "PULL_ARMED")
        r = bot.command(rw10(0x28, 0, 4), 4 * BLOCK)
        results["read_pulled_last"] = {k: r[k] for k in ("bytes", "stalled", "status", "residue")}
        assert results["read_pulled_last"] == {"bytes": 2048, "stalled": False, "status": 0, "residue": 0}
        assert [bot.tur(), bot.sense()] == [1, NOT_PRESENT]
        present(1)
        assert [bot.tur(), bot.sense(), bot.tur()] == [1, UA, 0]

        # WRITE(10) of 4 blocks at LBA 8, pulled after the first block is written.
        device("pull 1 0", "PULL_ARMED")
        payload = bytes([0x5C]) * (4 * BLOCK)
        r = bot.command(rw10(0x2A, 8, 4), 4 * BLOCK, data_in=False, payload=payload)
        results["write_pulled"] = {k: r[k] for k in ("stalled", "status", "residue")} | {
            "counts": counts(), "sense": bot.sense(),
            "block8_written": block(8) == checksum(payload[:BLOCK]), "block9_kept": block(9) == checksum(original(9))}
        w = results["write_pulled"]
        assert (w["stalled"], w["status"], w["residue"]) == (True, 1, 1536)
        assert w["counts"][:4] == (0, 1, 0, 0) and w["sense"] == NOT_PRESENT
        assert w["block8_written"] and w["block9_kept"]
        present(1)
        assert [bot.tur(), bot.sense(), bot.tur()] == [1, UA, 0]
        usb.util.release_interface(dev, 0)
    finally:
        RESULTS.parent.mkdir(exist_ok=True)
        RESULTS.write_text(json.dumps(results, indent=1, default=str))
        dut.write("stop")
