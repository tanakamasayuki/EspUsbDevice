# build probe

> English: [README.md](README.md)

未使用の機能がリンクされないことを確かめる、build 専用の sketch です。pytest では実行しません。
repo の root で compile し、Flash / RAM の値と、ELF に残る symbol（`nm`）を見ます。

```sh
arduino-cli compile --profile s3_device tests/build_probes/include_only
```

| probe | 目的 |
| --- | --- |
| `no_library` | library なしの基準 |
| `include_only` | include だけでは費用が増えない（`no_library` と Flash / RAM が同じ） |
| `device_only` | Device の宣言だけの費用。`begin()` 前に重い資源を確保しない |
| `vendor_copy`、`cdc_copy`、`midi_packet`、`msc_only`、`audio_only`、`video_only`、`ncm_only`、`dfu_only` | 選んだ class 以外の symbol が ELF に残らない |
| `controller_storage` | DWC2 の転送 record などの開始時領域の大きさ。profile は `s2_device` と `p4_device` |
