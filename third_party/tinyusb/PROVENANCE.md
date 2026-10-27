# USB backend provenance

[日本語](PROVENANCE.ja.md)

The v3 build has no TinyUSB vendor snapshot, configuration headers, class drivers,
OS/FIFO framework, or automatic upstream updater. Core, EP0, class dispatch and
runtime resource ownership live in this library. This directory archives origins
and licenses; it is not a build dependency or a version pin to update.

The hardware implementation is a maintained, derived fork, not a clean-room
rewrite. `ORIGIN.json` records the full upstream SHA and last local commit with
the selected vendor files. MIT copyright notices remain in derived sources;
`LICENSE-ST` retains the BSD-3-Clause terms for ST register definitions.

| Maintained source | Origin and scope |
| --- | --- |
| `src/internal/EspUsbDwc2Device.c` | TinyUSB DWC2 Device DMA/endpoint algorithm, with native runtime storage, locking, cancellation and interval validation |
| `src/internal/EspUsbDwc2Phy.c` | DWC2 reset, PHY selection and turnaround |
| `src/internal/EspUsbDwc2Hardware.h` | Register access and FIFO flush primitives |
| `src/internal/EspUsbDwc2Registers.h` | Device-only register view; unused Host/channel/FIFO windows removed |
| `src/internal/EspUsbDwc2Platform.*` | ESP metadata, IRQ/cache/pulls adapted from the former MIT ESP port |

Other derived layouts/data retain source attribution: controller events, CDC/MIDI
descriptor layouts and the selected US keyboard lookup rows. Deleted sources,
patch manifests and the former updater remain available in Git history.

Run `python3 tools/verify_usb_backend.py` offline to check maintained paths and
absence of vendor build/configuration paths. This check does not establish USB
correctness or detect every possible indirect dependency. Review compiler
headers, link maps/symbol locations, and run `validation/` with the standard
plugin `--combo all`. Upstream fixes require review and an explicit change to
the maintained implementation; there is no automatic snapshot replacement.

Runtime transfer records remain selected by descriptors. The Device-only map
has explicit register offset/stride assertions; the S3 probe additionally checks
key offsets against the SDK register structure. Invalid periodic intervals are
rejected before FIFO allocation. Interval/retry counters can represent 32768
without shifting by a negative count or truncating to eight bits.

This is a source/dependency ownership milestone, not completion of v3. Remaining
EP0/HALT concurrency, final API/examples, OS interoperability and real S2/P4/HS
validation are separate work. S31 requires SDK metadata/support and hardware.
