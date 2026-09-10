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
- `loopback/` 用の ESP32-P4 ボード

## 構成

- `unit/`: ボードを一切使わない。純粋な Python か、`src/` から出荷される C++ を
  抽出してシステムの g++ でコンパイルするかのどちらか。5 秒ほどで終わり、CI が
  push ごとに `.env` なしで回している（`.github/workflows/unit-tests.yml`）。
- `single/`: デバイス側ボード 1 枚のみ、USB ホスト役は使わない。実機にスケッチを
  書き込んで本物の API を呼び、`OK` / `NG` を出力する。`.env` が必要。
- `peer/`: EspUsbHost を host、EspUsbDevice を device とする2台構成テスト。
- `loopback/`: ESP32-P4 1台で EspUsbHost と EspUsbDevice を同時に動かすテスト。
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

`EspUsbHost` / `EspUsbDevice` のライブラリバージョンを上げた直後、または
release profile と local profile を切り替えた直後は、古い build cache / 中間生成物が残って
起動時クラッシュや不自然な timeout になることがあります。その場合は `--clean` を付けて
再ビルドします。

```sh
uv run --env-file .env pytest peer/ --profile=s3_peer_host --clean
uv run --env-file .env pytest loopback/ --profile=p4_loopback --clean
```

## peer テストの形

`peer/` 配下のモジュールはそれぞれ pytest のテスト 1 個です。中のケースはただの
名前付き関数で、リストから順に呼び出します。

```python
def _enumeration(dut, device): ...
def _keyboard(dut, device): ...


def test_composite_hid_cdc(dut, peers):
    device = peers["device"]

    device.write("?")
    device.expect_exact("DEVICE_READY 1")

    for check in (_enumeration, _keyboard):
        check(dut, device)
```

理由は 2 つです。失敗したとき行番号ではなく関数名で場所が分かること。そして
1 モジュール 1 テストなら、特定の順序でしか通らないテストが存在し得ないこと。

### 待つのではなく訊く

両側のスケッチは、起動時に一度告知するのではなく、訊かれたら答えます。

- デバイス側は `?` に `DEVICE_READY <0|1>` で答えます。答える前に `waitForHost()`
  で `device.ready()`（`tud_mounted()`、ホストが SET_CONFIGURATION を完了した
  状態）を待ちます。モジュール固有の状態は `DEVICE_NET` / `DEVICE_CABLES` /
  `DEVICE_NKRO` のように別行にし、`DEVICE_READY` 行には足しません。
- ホスト側も同様に `waitForDevice()` で `onDeviceConnected` がラッチしたアドレス
  を待ちます。コールバック出力を流すだけで接続を報告するコマンドを持たない
  スケッチには `?` → `HOST_READY <0|1> vid=.... pid=....` を追加しました。
- 列挙時に一度だけ得られる情報は、表示するだけでなく保持して訊き直せるように
  しました。`D` は HID レポートディスクリプタの要約、`S` はオーディオストリーム
  一覧を再送します。

これで実行位置が無関係になります。起動時に一度出る行は最初のテストにしか見えま
せんが、質問はいつでもできて、訊くこと自体がバナー待ちと同じ検証になります。

`peer/usb_msc` は最初からこの形で、他が起動バナーを読んでいた頃に逆順実行を唯一
通過したモジュールです。

`loopback/` は意図的にバナー待ちのままです。あちらは各モジュールがテスト 1 個で専用の
書き込みを持つため、読む行より先に走るものが存在しません。理由と、それが成り立たなくなる
条件は [loopback/README.ja.md](loopback/README.ja.md) にあります。

### 意図的に順序があるモジュール

4 つのモジュールは意図してケース順を固定しており、docstring にその理由を書いて
います。

- `usb_serial` — 最後の line coding 手順が「部分的な SET_LINE_CODING が他の
  フィールドを保つ」ことを見るので、前の手順が前提。
- `usb_serial_multi` — ポート分離の確認が、前の 2 ケースが作ったカウンタを読む。
- `usb_midi_cables` — 最初と最後のケースが、間の全送信を挟む before/after の対。
- `usb_vendor` — 最初のケースがデバイス側 RX 数の厳密値を見るので、カウンタが
  セッション開始時のままである必要がある。

それ以外はケースのリストを逆順にしても通るはずです。確認方法はタプルを
`reversed(...)` で包んでそのモジュールを再実行するだけ。pytest 1 回・書き込み
1 回で済むので、ケースを書き換えたモジュールには回しておく価値があります。

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
  本物の失敗は必ずファイル名と行番号を出します。`tools/build_check.py` はこれを判別して
  「signal で殺された、再実行してから調べろ」と明示します。
- **ビルドだけの負荷は pass を fail に変えませんが、タイミングの expect は狂わせます。**
  誰かがタイミングを計測している間は重いビルドを止め、そのことを伝えてください。
- **「実行が次のテストへ進んだ」を「そのテストが通った」と読まないでください。** pytest は
  失敗しても次のパラメータへ進みます。位置ではなく結果を見ること。

`pytest --clean` を引数なしで実行したときの収集順は loopback → peer → single → unit です。
example のビルドはこの実行に含まれません——`tools/build_check.py` と CI の Build Check
ワークフローが担当します。

各テスト終了時に、Host 側の `dut.log` と peer 側の `peer-*.log` が自動的に監査されます。
ESP-IDF のエラーログ、`ESP_ERR_*`、panic、assert、watchdog などの疑わしい行は、テストを
失敗させずに端末の `serial log audit` サマリーへ集計されます。HTML レポートを有効にしている
場合は、該当テストの展開ログにも追加されます。完全なシリアルログは
`/tmp/pytest-embedded/` 以下に保存されます。

現在のカバレッジと追加予定は [TEST_PLAN.ja.md](TEST_PLAN.ja.md) を参照してください。
