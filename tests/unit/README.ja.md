# Unit テスト

> English: [README.md](README.md)

unit テストでは、ホストに依存しないロジックを検証します。

- device descriptor の byte 列。
- configuration descriptor layout。
- FS / HS endpoint MPS 選択。
- HID report descriptor の byte 列。
- HID keyboard / mouse report builder。

## `compile_smoke`

最初の環境確認用テストです。`--run-mode=build` で Arduino CLI、sketch.yaml、
ESP32 board package、ライブラリ解決、公開ヘッダの最小コンパイルを確認します。
USB device stack の実行確認ではありません。

## `descriptor`

USB device / configuration / HID report descriptor の byte 列を検証します。
初期仕様として、HID keyboard と HID mouse の interrupt endpoint MPS は FS / HS とも
8 bytes に固定します。keyboard + mouse composite は単一 HID interface + report ID 構成で、
report ID 付き keyboard report に合わせて endpoint MPS を 16 bytes にします。
