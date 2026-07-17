# テスト

> English: [README.md](README.md)

このディレクトリには EspUsbDevice のテスト仕様と pytest-embedded 自動テストを置きます。

構造は意図的に EspUsbHost と揃えます。既存 peer テストの device 側を
Arduino-ESP32 USB device sketch から EspUsbDevice sketch へ段階的に移行するためです。
ESP32-P4 の loopback は、Arduino-ESP32 標準 Device 実装が HS 固定で FS host 側と組み合わせにくいことが分かったため、
`EspUsbDevice` で port / speed / endpoint MPS を明示制御する構成を主対象にします。

## 必要なもの

- `uv`
- Arduino CLI
- 対象ボード用の ESP32 board package
- `peer/` 用の ESP32-S3 ボード
- `loopback/` と `probe/` 用の ESP32-P4 ボード

## 構成

- `unit/`: ホスト不要の descriptor / report helper / FAT RAM disk テスト。
- `examples_compile/`: examples sketch の build-only smoke テスト。
- `peer/`: EspUsbHost を host、EspUsbDevice を device とする2台構成テスト。
- `loopback/`: ESP32-P4 1台で EspUsbHost と EspUsbDevice を同時に動かすテスト。
- `probe/`: P4 の port / speed 切り分け用スケッチ。
- `manual/`: 物理デバイスまたは目視確認が必要なテスト。

## 実行

このディレクトリから実行します。

```sh
uv run --env-file .env pytest
uv run --env-file .env pytest peer/
uv run --env-file .env pytest --run-mode=build
uv run --env-file .env pytest examples_compile/
```

通常の peer / loopback はリリース版 `EspUsbHost` を使います。local profile は
Host 側の未リリース修正をリリース前検証する場合だけ使います。

```sh
uv run --env-file .env pytest peer/ --profile=s3_peer_local
uv run --env-file .env pytest loopback/ --profile=p4_loopback_local
```

`EspUsbHost` / `EspUsbDevice` のライブラリバージョンを上げた直後、または
release profile と local profile を切り替えた直後は、古い build cache / 中間生成物が残って
起動時クラッシュや不自然な timeout になることがあります。その場合は `--clean` を付けて
再ビルドします。

```sh
uv run --env-file .env pytest peer/ --profile=s3_peer_host --clean
uv run --env-file .env pytest loopback/ --profile=p4_loopback --clean
```

各テスト終了時に、Host 側の `dut.log` と peer 側の `peer-*.log` が自動的に監査されます。
ESP-IDF のエラーログ、`ESP_ERR_*`、panic、assert、watchdog などの疑わしい行は、テストを
失敗させずに端末の `serial log audit` サマリーへ集計されます。HTML レポートを有効にしている
場合は、該当テストの展開ログにも追加されます。完全なシリアルログは
`/tmp/pytest-embedded/` 以下に保存されます。

現在のカバレッジと追加予定は [TEST_PLAN.ja.md](TEST_PLAN.ja.md) を参照してください。
