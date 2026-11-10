# PC tests

> 日本語: [README.ja.md](README.ja.md)

A Linux PC is the Host and checks the Device's behaviour on the wire. The PC side builds raw control and bulk transfers with pyusb (libusb); sg3_utils and dfu-util are not used.

| Probe | Purpose |
| --- | --- |
| `msc_media` | MSC media change and removal, checked with the PC's BOT client (06/28/00, 02/3A/00, residue) |
| `msc_fat` | The PC reads `EspUsbDeviceMscFatRamDisk` volumes of 1 / 4 / 24 MiB and checks them with `fsck.fat`. Needs PSRAM |
| `dfu_detach` | DFU DETACH acceptance and the time until the restart |
| `bulk_stream` | Streams blocks (27,136 and 4,096 bytes) with borrowed sends and checks that the PC receives every block in order. Records the throughput with one and two sends outstanding in `.pytest-results/bulk-stream.json` (no pass/fail on speed). P4X only |

`pc_bot.py` is the BOT client the MSC tests share. It expects a bulk MPS of 512 at HS and 64 at FS.

## Pairs

| Combo | Board | Link |
| --- | --- | --- |
| `p4x_pc` | The P4X HS port (bench 3-3 / 3-4) | HS |
| `primary=s2_pc` (unnamed combo) | The S2 native USB | FS; `msc_media` and `dfu_detach` only |

```sh
uv run --locked --env-file .env pytest pc --combo p4x_pc -q -rs
uv run --locked --env-file .env pytest pc/msc_media pc/dfu_detach --combo primary=s2_pc -q -rs
```

- The S2-to-PC pair is not permanent. Run it only when the S2's native USB is on the PC and `TEST_SERIAL_PORT_S2_PC` is set in `.env`.
- When an OS class driver (usb-storage and the like) binds first, the test detaches it explicitly.
- The VID/PIDs an ordinary user may open are limited by udev rules (`1209:0001` and others); the PC test sketches use those. A new VID/PID needs a new udev rule.
