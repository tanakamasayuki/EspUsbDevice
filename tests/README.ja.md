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

## 治具を共有するときの注意

治具は他のプロジェクトや他の Claude セッションと共有しています。実機を使う前に相手へ
連絡し、終わったら通知してください。以下は 2026-09-08 に実際に踏んだもので、いずれも
本物の不具合に見えて全部が環境要因でした。

- **治具に対して pytest は常に 1 プロセスだけ**にしてください。別ボードでも同時に走らせない
  こと。時間短縮のつもりで peer と loopback を並行させたところ、`arduino-cli upload` の失敗と
  `usb_ncm` の 90 秒タイムアウト（ログ上は最後の tick までスループット健全）という、本物に
  見える失敗が 2 件出ました。単独・逐次で回し直すと両方通ります。
- **P4 のポートロックは待たずに upload が即失敗します。** `Could not exclusively lock port ...
  Resource temporarily unavailable` が出たら、そのテストを単独で回し直してから判断してください。
  plugin の device lock は pytest 同士しか調停しないので、別プロジェクトの生の esptool が
  握っている場合はこの形になります。**upload の retry は提案済みで、plugin 側が入れない判断を
  しています**——相手が書き込み中なら P4 の 1〜2 MB イメージで 15〜40 秒はロックされ、数秒の
  backoff では同じ失敗に遅延を足すだけ、シリアルモニタが握っている場合は永久に解放されない
  ため。共有が常態化するなら、正しい直し方は相手側の esptool を同じ device lock
  （plugin の lock ディレクトリにある portalocker のファイルロック。キーは解決後のポートパス）
  に参加させることです。
- **隣のプロジェクトに kill された compile は `returncode=-15` かつコンパイラの診断が出ません。**
  本物の失敗は必ずファイル名と行番号を出します。`examples_compile` はこれを判別して
  「signal で殺された、再実行してから調べろ」と明示します。
- **ビルドだけの負荷は pass を fail に変えませんが、タイミングの expect は狂わせます。**
  誰かがタイミングを計測している間は重いビルドを止め、そのことを伝えてください。
- **「実行が次のテストへ進んだ」を「そのテストが通った」と読まないでください。** pytest は
  失敗しても次のパラメータへ進みます。位置ではなく結果を見ること。

`pytest --clean` を引数なしで実行したときの収集順は examples_compile → loopback → peer → unit
なので、最初の 30 分ほどはボードに触りません。

各テスト終了時に、Host 側の `dut.log` と peer 側の `peer-*.log` が自動的に監査されます。
ESP-IDF のエラーログ、`ESP_ERR_*`、panic、assert、watchdog などの疑わしい行は、テストを
失敗させずに端末の `serial log audit` サマリーへ集計されます。HTML レポートを有効にしている
場合は、該当テストの展開ログにも追加されます。完全なシリアルログは
`/tmp/pytest-embedded/` 以下に保存されます。

現在のカバレッジと追加予定は [TEST_PLAN.ja.md](TEST_PLAN.ja.md) を参照してください。
