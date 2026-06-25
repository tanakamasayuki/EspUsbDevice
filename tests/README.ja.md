# テスト

> English: [README.md](README.md)

このディレクトリには EspUsbDevice のテスト仕様と、今後追加する
pytest-embedded 自動テストを置きます。

構造は意図的に EspUsbHost と揃えます。既存 peer テストの device 側を
Arduino-ESP32 USB device sketch から EspUsbDevice sketch へ段階的に移行するためです。

## 必要なもの

- `uv`
- Arduino CLI
- 対象ボード用の ESP32 board package
- `peer/` 用の ESP32-S3 ボード
- `loopback/` と `probe/` 用の ESP32-P4 ボード

## 構成

- `unit/`: ホスト不要の descriptor / report helper テスト。
- `peer/`: EspUsbHost を host、EspUsbDevice を device とする2台構成テスト。
- `loopback/`: ESP32-P4 1台で host / device を動かすテスト。
- `probe/`: P4 の port / speed 切り分け用スケッチ。
- `manual/`: 物理デバイスまたは目視確認が必要なテスト。

## 実行

このディレクトリから実行します。

```sh
uv run --env-file .env pytest
uv run --env-file .env pytest peer/
uv run --env-file .env pytest --run-mode=build
```

通常の peer / loopback はリリース版 `EspUsbHost` を使います。local profile は
Host 側の未リリース修正をリリース前検証する場合だけ使います。

```sh
uv run --env-file .env pytest peer/ --profile=s3_peer_local
uv run --env-file .env pytest loopback/ --profile=p4_loopback_local
```

具体的なテストはライブラリ実装に合わせて追加します。
