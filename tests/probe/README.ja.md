# Probe テスト

> English: [README.md](README.md)

`tests/probe` は ESP32-P4 USB の bring-up と port / speed 切り分け用です。
probe sketch は安定した回帰テストではなく、ハードウェア、SDK、ホスト OS の観測結果を
記録するために使います。

初期 probe:

- `p4_device_fs_probe`: Full Speed USB device として列挙されるか確認する。
- `p4_device_hs_probe`: High Speed USB device として列挙されるか確認する。
- `p4_loopback_matrix_probe`: host / device の port と speed の組み合わせを出力する。
