"""FAT RAM disk volumes of several sizes, read back by the PC and checked by fsck.fat.

FAT12 holds fewer than 4085 clusters, so the cluster must grow with the volume
for a large disk (in PSRAM) to stay valid.
"""
import pathlib
import struct
import subprocess
import sys

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from pc_bot import Bot, rw10  # noqa: E402

import usb.core  # noqa: E402
import usb.util  # noqa: E402
import time  # noqa: E402

SERIAL = "espusb-msc-fat"
BLOCK = 512
FILE_BYTES = 300 * 1024


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


def read_volume(bot, blocks):
    image = bytearray()
    for lba in range(0, blocks, 64):
        n = min(64, blocks - lba)
        r = bot.command(rw10(0x28, lba, n), n * BLOCK)
        assert r["status"] == 0 and r["bytes"] == n * BLOCK
        image += r["data"]
    return bytes(image)


def fat12_file(image, name):
    bps, spc, reserved, fats, roots, total16, _, spf = struct.unpack_from("<HBHBHHBH", image, 11)
    root = (reserved + fats * spf) * bps
    data = root + roots * 32
    for i in range(roots):
        entry = image[root + i * 32: root + i * 32 + 32]
        if entry[:11] == name:
            cluster, size = struct.unpack_from("<HI", entry, 26)
            out = bytearray()
            while 2 <= cluster < 0xFF8 and len(out) < size:
                start = data + (cluster - 2) * spc * bps
                out += image[start:start + spc * bps]
                fat = reserved * bps + cluster * 3 // 2
                value = struct.unpack_from("<H", image, fat)[0]
                cluster = value >> 4 if cluster & 1 else value & 0xFFF
            return bytes(out[:size])
    raise AssertionError(f"{name!r} not in the root directory")


@pytest.mark.parametrize("kib", [1024, 4096, 24576])
def test_msc_fat(dut, kib, tmp_path):
    try:
        dut.write(f"start {kib}")
        blocks = int(dut.expect(r"STARTED ok=1 blocks=(\d+)\r\n", timeout=60).group(1))
        assert blocks == kib * 1024 // BLOCK
        bot = Bot(find())
        image = read_volume(bot, blocks)
        usb.util.dispose_resources(bot.dev)

        bps, spc = struct.unpack_from("<HB", image, 11)
        reserved, fats, roots, total16 = struct.unpack_from("<HBHH", image, 14)
        spf = struct.unpack_from("<H", image, 22)[0]
        total32 = struct.unpack_from("<I", image, 32)[0]
        total = total16 or total32
        clusters = (total - reserved - fats * spf - roots * 32 // bps) // spc
        assert bps == BLOCK and total == blocks and image[54:62] == b"FAT12   "
        assert clusters < 4085, (spc, clusters)
        assert spc == 1 or (total - reserved - fats * spf - roots * 32 // bps) // (spc // 2) >= 4085

        path = tmp_path / "volume.img"
        path.write_bytes(image)
        check = subprocess.run(["/usr/sbin/fsck.fat", "-n", "-v", str(path)], capture_output=True, text=True)
        assert check.returncode == 0, check.stdout + check.stderr

        big = fat12_file(image, b"BIG     BIN")
        assert big == bytes((i * 7 + (i >> 9)) & 0xFF for i in range(FILE_BYTES))
        assert fat12_file(image, b"README  TXT") == b"EspUsbDevice FAT12\r\n"
        print(kib, "KiB: sectors/cluster", spc, "clusters", clusters)
    finally:
        dut.write("stop")
        dut.expect_exact("STOPPED heap=1\r\n", timeout=30)
