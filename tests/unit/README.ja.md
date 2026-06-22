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
