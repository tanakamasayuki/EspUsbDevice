# テスト

> English: [README.md](README.md)

このディレクトリには、EspUsbDevice の実機試験（pytest-embedded-arduino-cli）を置きます。
確かめた範囲と未確認の範囲は [TEST_PLAN.ja.md](TEST_PLAN.ja.md) にあります。

## 必要なもの

- `uv`
- Arduino CLI
- ESP32 の board package（Arduino-ESP32 3.3.12。各 `sketch.yaml` で固定しています）
- 結線した bench（[bench](#bench)）

## 構成

| 場所 | 内容 |
| --- | --- |
| `peer/` | 2 台構成。Host（EspUsbHost）と Device（EspUsbDevice）を別の board で動かす。S3 / S2 の FS と、P4 / P4X の HS 直結で同じ probe が動く。[peer/README.ja.md](peer/README.ja.md) |
| `loopback/` | ESP32-P4 1 台の HS 口と FS 口をつなぎ、片方を Host、片方を Device にする。[loopback/README.ja.md](loopback/README.ja.md) |
| `single/` | board 1 枚だけで確かめる。[single/README.ja.md](single/README.ja.md) |
| `pc/` | Linux PC が Host。PC 側は pyusb で生の転送を組む。[pc/README.ja.md](pc/README.ja.md) |
| `build_probes/` | build 専用。未使用の機能がリンクされないことを確かめる。pytest の対象外。[build_probes/README.ja.md](build_probes/README.ja.md) |
| `sketch_support/` | 複数の probe が共有する header |

- 各 probe は directory 一つです。Host 側の sketch と `test_*.py`、Device 側の `peer_device/` を持ちます。
- `examples/` は読み込みません。examples の確認は build だけで、実機の合否は probe で決めます。

## 実行

このディレクトリから実行します。

```sh
uv sync --locked
cp .env.example .env   # 各 profile の serial port を書く
uv run --locked --env-file .env pytest --combo all --collect-only -q
uv run --locked --env-file .env pytest --combo all -q -rs --junitxml=.pytest-results/all.xml
```

| したいこと | コマンド |
| --- | --- |
| 一つの combo だけ | `pytest --combo s3 -q -rs` |
| 一つの probe だけ | `pytest peer/vendor_link --combo p4_p4x_hs -q -rs` |
| 最初の失敗で止める | `pytest --combo all --maxfail=1 -q -rs` |
| build だけ | `pytest --combo all --run-mode=build -q` |

（どれも `uv run --locked --env-file .env` を前に付けます。）

- 実機の pytest は一つずつ実行します。同じ board を複数の combo が使います。
- 件数は試験の追加で変わります。毎回 `--collect-only` で収集数とエラー 0 を確かめます。
- 既定では、失敗しても最後まで流します。各 probe は module の始めに書き込み直すので、前の失敗の状態を持ち越しません。
- 一つの失敗を調べるときは `--maxfail=1` を付けます。最初の失敗の後に、壊れた状態の board で後続を走らせて、原因の違う失敗を重ねないためです。

### offline の確認

```sh
python3 tools/verify_usb_backend.py   # repo の root で
```

保守対象の DWC2 file があること、旧 vendor の path・設定 header・`tusb*.h` の include がないことを確かめます。
CI（`.github/workflows/tests.yml`）は、これと `pytest --combo all --collect-only` を実行します。

### 結果の見方

- 成果物は `.pytest-results/`（git 対象外）に置きます。
- 終了値 0 だけを合格にしません。収集数、JUnit の passed / skipped / failed / error、未完了の数、各 serial log が停止したこと（panic がないこと）を確かめます。
- skip は「未検証」であって合格ではありません。
- serial の原本は plugin の出力先（`/tmp/pytest-embedded/`）にあります。最新の directory ではなく、JUnit の時刻から該当するものを探します。

## 方針

- conftest も独自の pytest plugin も置きません。`--noconftest` で conftest を読まず、標準 plugin [pytest-embedded-arduino-cli](https://github.com/tanakamasayuki/pytest-embedded-arduino-cli)（1.10.0 以降）の combo、`dut`、`peers` だけを使います。
- test 本文の中で、開始・通信・停止を明示的に書きます。
- 結線は plugin の combo で表します。profile の直積や、独自の skip hook で表しません。
- 通信の失敗を retry や skip で隠しません。
- plugin に足りない機能があれば、再現例と受入条件をまとめて plugin 側へ依頼します。conftest で補いません。

## combo

`pyproject.toml` の `arduino_combos` に、実際に結線した組だけを登録します。`--combo all` は、各 sketch が対応する profile を持つ組だけを順に実行します。

| combo | primary（Host 側） | peer device | link |
| --- | --- | --- | --- |
| `s3` | `s3_host` | `s3_device` | FS |
| `s3_s2` | `s3b_host`（S3） | `s2_device`（S2） | FS |
| `s2_s3` | `s2_host`（S2） | `s3b_device`（S3） | FS |
| `p4` | `p4_loopback` | なし | FS（同じ P4 の HS 口と FS 口） |
| `p4_psram` | `p4_psram` | なし | FS（`p4` と同じ board、PSRAM 有効の firmware） |
| `p4_p4x_hs` | `p4_hs_host`（旧 P4） | `p4x_hs_device`（P4X） | HS |
| `p4x_p4_hs` | `p4x_hs_host`（P4X） | `p4_hs_device`（旧 P4） | HS |
| `p4x_pc` | `p4x_hs_pc`（P4X） | なし（Linux PC が Host） | HS |

- 対応する profile のない組は展開されません。どの組にも合わない test は `combo=none` で skip されます。
- port が未設定の場合や、同じ実機に二役を割り当てた場合も、build の前に skip されます。
- 設定済みの port への接続失敗、build / upload の失敗は error、通信の不一致は failed です。
- 常設していない結線は、名前のない combo で流します（例：`--combo primary=s2_pc`。[pc/README.ja.md](pc/README.ja.md)）。
- 直積の `--profiles=all --peer-profiles=all` は使いません。結線のない組を作ってしまうためです。

### profile

- `sketch.yaml` に profile を書きます。Device 側は library を `dir:` で参照し、Host 側は公開版の EspUsbHost（`EspUsbHost (2.10.1)`）を版指定で使います。
- profile 名は「SoC + 役割」です（例：`p4x_hs_device`）。同じ SoC の別の board は `s3b_` のように区別します。MAC や tty 番号は名前に入れません。
- 一部の probe は `p4_device`、`p4x_*_compile` のような compile 確認用の profile を持ちます。combo には入っていないので、pytest では実行されません。

## `.env`

試験用の USB 結線とは別に、書込み・log 用の serial port を各 board に用意し、`.env` で profile に割り当てます。key の一覧は `.env.example` にあります。

| key | 割り当てる board |
| --- | --- |
| `TEST_SERIAL_PORT_S3_HOST` / `TEST_SERIAL_PORT_PEER_DEVICE_S3_DEVICE` | S3 二台 |
| `TEST_SERIAL_PORT_S3B_HOST` / `TEST_SERIAL_PORT_PEER_DEVICE_S3B_DEVICE` | S2 と組む S3 |
| `TEST_SERIAL_PORT_S2_HOST` / `TEST_SERIAL_PORT_PEER_DEVICE_S2_DEVICE` | S3 と組む S2 |
| `TEST_SERIAL_PORT_P4_LOOPBACK` / `TEST_SERIAL_PORT_P4_PSRAM` | loopback の旧 P4（同じ board） |
| `TEST_SERIAL_PORT_P4_HS_HOST` / `TEST_SERIAL_PORT_PEER_DEVICE_P4_HS_DEVICE` | HS 直結の旧 P4 |
| `TEST_SERIAL_PORT_P4X_HS_HOST` / `TEST_SERIAL_PORT_PEER_DEVICE_P4X_HS_DEVICE` | HS 直結の P4X |
| `TEST_SERIAL_PORT_P4X_HS_PC` | Linux PC につないだ P4X |
| `TEST_SERIAL_PORT_S2_PC` | Linux PC につないだ S2（常設しない。必要なときだけ） |

- primary は `TEST_SERIAL_PORT_<PROFILE>`、peer device は `TEST_SERIAL_PORT_PEER_DEVICE_<PROFILE>` です。
- port は `/run/board-identify/by-id/...` のような安定した path を使います。
- 同じ物理 board を複数の profile に割り当ててよいのは、別の combo どうしの間だけです。

## bench

| 位置 | chip | PSRAM | 制御 / log | 結線 | combo |
| --- | --- | --- | --- | --- | --- |
| S3 二台 | ESP32-S3 | — | 各 board の書込み口 | native OTG どうしを FS で直結 | `s3` |
| S2–S3 | ESP32-S2 と ESP32-S3 | — | 各 board の CH340 UART | native USB どうしを FS で直結 | `s3_s2`、`s2_s3` |
| 2-1 | ESP32-P4 v1.3（旧 P4） | 32 MiB | CH343 UART | 同じ board の HS 口と FS 口を結線（loopback） | `p4`、`p4_psram` |
| 2-3 | ESP32-P4 v1.3（旧 P4） | 32 MiB | CH340 UART | 2-4 と HS 口どうしを直結 | `p4_p4x_hs`、`p4x_p4_hs` |
| 2-4 | ESP32-P4 v3.2（P4X） | 32 MiB | USB Serial/JTAG | 2-3 と HS 直結 | `p4_p4x_hs`、`p4x_p4_hs` |
| 3-3 / 3-4 | ESP32-P4 v3.2（P4X） | 32 MiB | 3-3 の USB Serial/JTAG | 3-4 の HS 口を Linux PC へ直結 | `p4x_pc` |

位置 `N-M` はハブ N の port M です。設備の正本は bench の台帳（`oep-bench/hosts/native-linux/` の README と `inventory.json`）です。

- 書込み・log の port は、試験用の USB data 結線とは別です。試験 data を log の経路に流しません。
- 同じ board を使う組（`p4` と `p4_psram`、HS 直結の二方向）は同時に動かしません。
- 2-3 の HS 口から 2-4 へ電源が回り込みます。片方の電源を切るときは、両方の port をまとめて切ります。
- 機器の lock は plugin が取ります。bench は他のプロジェクトと共有しているので、plugin の外から board を使いません。
- 新しい組を足すときは、全 probe を build する前に `peer/pad_link` で二台の native 口の導通を確かめます。書込み口が見えていても、試験用の USB 結線がつながっているとは限りません。

## 新しい probe を足すとき

- primary の Host と `peer_device/` の Device という標準の構造を使い、test 関数は `dut` と `peers["device"]` を受け取ります。
- combo 名は Host を先に書きます。
- FS 前提の値（MPS 64、FS の FIFO、ISO の interval、endpoint 数）は、速度と能力から期待値を決めます。sketch では `sketch_support/probe_link.h` の `PROBE_BULK_MPS`、test では `"_hs_" in request.node.name` を使います。
- HS の試験で FS に fallback したら失敗にします。
- NCM は同じ MCU 内の lwIP 通信で合格にせず、raw frame と USB の転送量で wire 経由を確かめます。
- DFU は RAM backend で確かめます。実際の OTA / SD への書込みは別の試験範囲です。
- 置いたフォルダの README の probe 表に、目的を一行で足します。
