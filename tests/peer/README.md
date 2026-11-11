# Peer tests

> 日本語: [README.ja.md](README.ja.md)

The Host (the released EspUsbHost) and the Device (EspUsbDevice) run on separate boards whose native USB ports are linked directly.
Each probe holds the Host sketch, its `test_*.py` and the Device sketch in `peer_device/`.

## Combos

| Probe | Combo | Link |
| --- | --- | --- |
| Shared probes (the "Shared" table below) | `s3`, `s3_s2`, `s2_s3`, `p4_p4x_hs`, `p4x_p4_hs` | FS (S3 / S2) and HS (P4 / P4X) |
| HS-only probes (the "HS only" table below) | `p4_p4x_hs`, `p4x_p4_hs` | HS |
| `pad_link` | `s3`, `s3_s2`, `s2_s3` | FS pairs only |

- On the P4, `EspUsbController::Auto` and the Host's default port select the HS controller, so the shared probes use the same sketches.
- Values that depend on the link's MPS come from `PROBE_BULK_MPS` in `../../sketch_support/probe_link.h` in a sketch and from `"_hs_" in request.node.name` in a test.

```sh
uv run --locked --env-file .env pytest peer --combo s3 -q -rs
uv run --locked --env-file .env pytest peer/vendor_link --combo p4_p4x_hs -q -rs
```

## Shared

| Probe | Purpose |
| --- | --- |
| `vendor_roundtrip` | Vendor round trips; the same binary restarts with other capacities and transmit modes |
| `vendor_packet_size` | Vendor FS bulk packet sizes: the 8 / 16 / 32 / 64 that USB 2.0 allows work, any other fails `begin()` |
| `vendor_soak` | Numbered frames survive repeated Host re-enumeration with no loss, duplication or corruption |
| `transfer_limits` | A borrowed transfer longer than 16 bits passes with DMA splitting and a single completion |
| `cancel_lifecycle` | Cancelling a borrowed transfer, and racing it with reset / `end()`, returns the buffer exactly once |
| `direct_queue` | Two borrowed sends can be outstanding and a third is `Busy`; both go out in order, and cancel (by either id) or `end()` returns both once, in order |
| `cancel_fence` | Old completions left beyond the event queue's capacity never return a newer loan |
| `configuration_epoch` | After the configuration is selected again, stale queued completions never return a newer loan |
| `bulk_standard` | SET_INTERFACE and ENDPOINT_HALT return only that stream's loans and keep the others running |
| `allocation_failure` | Each allocation point of `begin()` fails in turn; the device returns to stopped and the next `begin()` succeeds |
| `function_registry` | Seven or more Functions, rebuilding and rolling back the registry, report IDs of seven HIDs |
| `controller_capacity` | A configuration with too many IN endpoints is refused before the IRQ and worker start, and the settings are kept |
| `endpoint_number` | Endpoints with gaps in their numbers get a packed TX FIFO |
| `descriptor_storage` | Descriptors are counted before they are written: large descriptors, strings, WebUSB, MS OS 2.0 |
| `control_core` | EP0, standard request checks, the event area at start |
| `reset_start` | On a real USBRST interrupt, stale work is invalidated before ENUMDNE and the worker's reset handling |
| `suspend_resume` | A real bus suspend / resume keeps the configuration and loans; the value of `suspended()` |
| `remote_wakeup` | The Host observes the Device's wake request; Host enable, clear and the reset boundary ([README](remote_wakeup/README.md)) |
| `api_semantics` | The API promises in the documentation (HID press and release, packet lengths, vendor request routing, the `end()` deadline, TEST_MODE). [TEST_PLAN](../TEST_PLAN.md#probes-for-the-public-api-contract) |
| `cdc_stream` | Two CDCs and a Vendor run independently over the shared bulk layer; line coding |
| `cdc_tx` | CDC `print` backpressure, TX timeout, safety inside the worker, `flush` waiting for the real completion |
| `midi_stream` | MIDI packet queue, cables, callbacks, per-direction modes |
| `hid_transport` | HID capacity derived from the report descriptor, multi-packet reports, control, boot mode |
| `hid_report_state` | Per-report-ID GET_REPORT snapshots, concurrent Feature updates, idle, reset |
| `hid_standard` | A multi-packet HID report recovers without changing the PID of the opposite endpoint |
| `hid_own_interface` | A Keyboard on its own boot interface beside a merged Mouse and Consumer Control: layout, report IDs, input, LEDs and SET_PROTOCOL per interface |
| `hid_protocol` | Switching protocol while an old-format IN is still in DMA; the Host reads the new report ([README](hid_protocol/README.md)) |
| `hid_typed` | Gamepad, Consumer and System Control sends and GET_REPORT ([README](hid_typed/README.md)) |
| `msc_bot` | The 13 BOT cases, invalid CBWs, the reset sequence, SCSI sense |
| `msc_storage` | MSC staging capacity, write protection, media state and errors, recovery; MSC and CDC traffic together |
| `msc_helpers` | The RAM disk and FAT12 RAM disk helpers through real BOT ([README](msc_helpers/README.md)) |
| `audio_stream` | Audio PCM buffers and isochronous transfers; 13 sample rates; changing the rate after `end()` |
| `audio_alternates` | The Host selects the alternate for each PCM shape and checks the isochronous payload; ISO OUT with gaps |
| `audio_events` | Optional event area, overflow, settings while stopped, Audio control requests |
| `audio_functions` | Four UAC1 duplex Functions share endpoint numbers across directions |
| `audio_cdc` | Periodic Audio and CDC bulk share one controller and worker |
| `audio_overflow` | The Playback overflow policy (`DropOldest` / `DropNewest`) matched frame by frame against a model. `test_iso_out_rate` measures the count of continuous ISO OUT received (no pass/fail) |
| `video_control` | UVC controls, frame ownership, MJPEG / YUY2 wire data; bulk HALT |
| `video_lifecycle` | Video format checks; DMA stops before a borrowed frame is returned |
| `ncm_stream` | NCM resources, packing into NTBs, interoperation with the Host |
| `ncm_ntb` | Raw NTB checks, independent NCM interfaces, alternate-setting recovery, per-direction HALT |
| `ccid_stream` | CCID capacity, two readers, no notification when the card is fixed; the default `messageBytes` |
| `dfu_transport` | An independent DFU interface, resources at start, the synchronous RAM backend |

## HS only

| Probe | Purpose |
| --- | --- |
| `hs_link` | HS enumeration in the combo's direction; how VBUS looks to the Device |
| `vendor_link` | MPS 512; copy echo and borrowed transfers at packet-boundary lengths; restart |
| `msc_midi` | MSC BOT on a RAM disk and MIDI with two cables |
| `cdc_hid` | CDC ACM echo and line coding with a boot keyboard in one device |
| `ccid` | A simulated card: ATR, APDU and Escape across packet boundaries; slot state |
| `dfu` | RAM backend download, manifest, abort, runtime detach |
| `psram_link` | MSC storage and Vendor rings in the Device's PSRAM, round trips at HS |
| `fifo_budget` | Five default Vendors enumerate; a sixth makes `begin()` return `ESP_ERR_INVALID_SIZE`. Per-Function `bulkInBuffering`: a Vendor beside three CDC ports is doubled alone with `Double`, and a `Double` that cannot fit is refused |

## Link check

| Probe | Purpose |
| --- | --- |
| `pad_link` | Without either USB stack, one side drives or pulls D+/D− through the USB_WRAP test registers and the other reads them, showing whether the two native ports are electrically joined. Run it on a new pair before building every probe |
