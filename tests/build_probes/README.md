# Build probes

> 日本語: [README.ja.md](README.ja.md)

Build-only sketches that check unused features are not linked. pytest does not run them.
Compile them from the repository root and look at the Flash / RAM figures and at the symbols left in the ELF (`nm`).

```sh
arduino-cli compile --profile s3_device tests/build_probes/include_only
```

| Probe | Purpose |
| --- | --- |
| `no_library` | Baseline without the library |
| `include_only` | Including the header costs nothing (same Flash / RAM as `no_library`) |
| `device_only` | The cost of declaring a Device; nothing heavy is allocated before `begin()` |
| `vendor_copy`, `cdc_copy`, `midi_packet`, `msc_only`, `audio_only`, `video_only`, `ncm_only`, `dfu_only` | No symbols of classes other than the chosen one remain in the ELF |
| `controller_storage` | The size of the start-time areas such as the DWC2 transfer records. Profiles `s2_device` and `p4_device` |
