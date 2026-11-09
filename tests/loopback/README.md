# Loopback tests

> 日本語: [README.ja.md](README.ja.md)

One P4 (P4 v1.x) has its native HS and FS ports wired together; one controller is the Host and the other the Device.
The Host side uses the released EspUsbHost.

| Device side | Host side | Actual link |
| --- | --- | --- |
| HS controller (UTMI) | FS controller | FS, 12 Mbps, bulk MPS 64 |
| FS controller | HS controller (UTMI) | FS, 12 Mbps, bulk MPS 64 |

- Device operation of both controllers is checked. The link is FS in either direction.
- **The 480 Mbps HS link is not tested here.** HS is covered by the HS combos (`p4_p4x_hs`, `p4x_p4_hs`, `p4x_pc`).
- When the HS-capable controller is the Device, bulk capacities are checked against 512 even though the link is FS; an MSC staging of 64, for example, is refused by `begin()` ([Values that change at high speed](../TEST_PLAN.md#values-that-change-at-high-speed)).
- The flashing and log port is separate from the USB link under test.

## Running

Set `TEST_SERIAL_PORT_P4_LOOPBACK` and `TEST_SERIAL_PORT_P4_PSRAM` in `.env`; both name the same board's port.

```sh
uv run --locked --env-file .env pytest loopback --combo p4 -q -rs          # every probe without PSRAM
uv run --locked --env-file .env pytest loopback/psram --combo p4_psram -q -rs
uv run --locked --env-file .env pytest loopback/audio --combo p4 -q -rs    # one probe
```

- `p4` and `p4_psram` use the same board, so run them one after the other.
- The `p4_psram` profile adds `PSRAM=enabled` to the FQBN.
- Some probes carry P4X compile-only profiles (`p4x_compile`, `p4x_audio_compile`, `p4x_psram_compile`). They are in no combo, so pytest never runs them.

## Probes

Each test starts and stops the Device several times on both controllers and confirms the final stop.

| Probe | Purpose |
| --- | --- |
| `basic` | Vendor copy round trips, repeated borrowed sends, borrowed receive → borrowed send (1 to 65537 bytes); a Vendor + CDC + Keyboard composite |
| `classes` | RAM MSC writes and reads (sectors of 511 / 512), the HS controller refusing a staging of 64 and recovering at 512; MIDI echo on two cables |
| `audio` | UAC1 / UAC2 isochronous playback and capture running together and compared; UAC2 also checks feedback updates |
| `video` | Frames sent over Bulk and ISO; content, completion status and actual length, the same buffer returned |
| `ccid` | ATR, APDU and Escape of a simulated card, both removable and fixed |
| `dfu` | RAM backend download, manifest, abort, runtime detach; GETSTATE / GETSTATUS and poll times |
| `ncm` | Raw Ethernet frames round-tripped at several NTB capacities, without IP or a netif |
| `psram` | MSC storage and the Vendor RX / TX rings in PSRAM; ring wraparound and guards outside the capacity |

## Not checked here

- The 480 Mbps HS link
- The P4X (covered by the HS combos)
- PC operating-system class drivers
- Real OTA, SD cards or codecs
- USB DMA reading or writing PSRAM directly (the product stages in internal RAM and refuses PSRAM buffers for borrowed transfers)

## Notes on writing these tests

- A borrowed receive lends DMA the whole posted capacity, not just the length received. Put guards outside the posted capacity.
- Borrowed buffers are in internal RAM on 64-byte boundaries.
- Vendor IN on the Host side is received through the released EspUsbHost's cache-prepared read queue and the `onVendorData` callback.
  The synchronous `vendorReadSync()` can corrupt the last received byte on the P4, an open Host-side issue ([TODO](../../TODO.ja.md)).
  The `start ... sync` command of `basic` exists to compare that synchronous path and is not used by the normal pytest run.
- Raw SDK Host transfers for DFU prepare the cache on the caller's side.
- For NCM, `networkClose()` → Device end → Host end makes the next Host start fail (an open Host-side issue). The test uses the normal order: Device disconnect, then Host end.
