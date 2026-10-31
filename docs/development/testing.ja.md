# 試験

保守者向けの文書です。v3 の実機試験の仕組み、bench、実行方法、確認済みの範囲を書きます。
個々の probe の目的は [validation/README.ja.md](../../validation/README.ja.md) にあります。

## 方針

- 試験の入口は `validation/` だけです。v2 の試験の helper や sketch は流用しません。
- 試験用の probe と、利用者向けの `examples/` を分けます。examples の確認は build だけで、実機の合否は probe で決めます。
- conftest も独自の pytest plugin も置きません。標準 plugin [pytest-embedded-arduino-cli](https://github.com/tanakamasayuki/pytest-embedded-arduino-cli) の機能だけを使います。
- test 本文の中で、開始・通信・停止を明示的に書きます。fixture は plugin の `dut` と `peers` だけです。
- 結線は plugin の combo で表します。profile の直積や、独自の skip hook で表しません。
- 通信の失敗を retry や skip で隠しません。skip は「未検証」であって合格ではありません。
- plugin に足りない機能があれば、再現例と受入条件をまとめて plugin 側へ依頼します。conftest で補いません。

## 仕組み

### `validation/pyproject.toml`

試験だけの Python 環境です。library 本体とは別の lock（`validation/uv.lock`）を持ちます。

| 設定 | 値 | 意味 |
| --- | --- | --- |
| 依存 | `pytest`、`pytest-embedded-arduino-cli>=1.10.0`、`pytest-embedded-serial`、`pyusb` | pyusb は Linux PC を Host にする試験だけが使う |
| `testpaths` | `s3`、`p4`、`p4_hs`、`p4x_pc`、`peer` | `build_probes/` は pytest の対象外 |
| `addopts` | `--noconftest --maxfail=1` | conftest を読まない。最初の失敗で止める |
| `arduino_combos` | 下表の 6 組 | 実際に結線した組だけ |

- `--noconftest`：どこかに conftest が置かれても読み込まないようにします。試験の挙動が test file と plugin だけで決まります。
- `--maxfail=1`：最初の失敗（USB の停止失敗を含む）の後は、次の実機 case を実行しません。壊れた状態の board で後続を走らせて、原因の違う失敗を重ねないためです。全件を流したいときは `--maxfail=1000` などで上書きします。

### combo

`arduino_combos` に、実際に結線した組だけを登録します。`--combo all` は、各 sketch が対応する profile を持つ組だけを順に実行します。

| combo | primary（Host 側） | peer device | link |
| --- | --- | --- | --- |
| `s3` | `s3_host` | `s3_device` | FS |
| `p4` | `p4_loopback` | なし | FS（同じ P4 の HS 口と FS 口） |
| `p4_psram` | `p4_psram` | なし | FS（`p4` と同じ board、PSRAM 有効の firmware） |
| `p4_p4x_hs` | `p4_hs_host`（旧 P4） | `p4x_hs_device`（P4X） | HS |
| `p4x_p4_hs` | `p4x_hs_host`（P4X） | `p4_hs_device`（旧 P4） | HS |
| `p4x_pc` | `p4x_hs_pc`（P4X） | なし（Linux PC が Host） | HS |

- `--combo all` では、対応する profile のない組は展開されません。どの組にも合わない test は `combo=none` で skip されます。
- port が未設定の場合や、同じ実機に二役を割り当てた場合も、build の前に skip されます。
- 設定済みの port への接続失敗、build / upload の失敗は error、通信の不一致は failed です。
- 直積の `--profiles=all --peer-profiles=all` は使いません。結線のない組を作ってしまうためです。
- 参照：[plugin 1.10.0 の profile の組合せと適用条件](https://github.com/tanakamasayuki/pytest-embedded-arduino-cli/blob/v1.10.0/TESTING_ADVANCED.ja.md#profile-の組合せと適用条件)

### profile と sketch の構成

- 各 probe は directory 一つです。Host 側の sketch と `test_*.py`、Device 側の `peer_device/` を持ちます。
- `sketch.yaml` に profile を書きます。Device 側は library を `dir:` で参照し、Host 側は公開版の EspUsbHost（`EspUsbHost (2.10.1)`）を版指定で使います。
- profile 名は「SoC 世代 + HS + 役割」です（例：`p4x_hs_device`）。MAC や tty 番号は名前に入れません。
- `s3/` の probe は、S3 の profile に加えて P4 / P4X の HS profile も持ちます。同じ probe が `s3`、`p4_p4x_hs`、`p4x_p4_hs` で動きます。
  - P4 では `EspUsbController::Auto` と Host の既定 port が HS controller を選ぶので、sketch の役割は変わりません。
  - link の MPS に依存する値は、sketch では `s3/common/probe_link.h` の `PROBE_BULK_MPS`、test では `"_hs_" in request.node.name` から決めます。
- 一部の probe は `s2_device`、`p4_device`、`p4x_*_compile` のような compile 確認用の profile を持ちます。combo には入っていないので、pytest では実行されません。

### `.env`

試験用の USB 結線とは別に、書込み・log 用の serial port を各 board に用意します。その port を `.env` で profile に割り当てます。

```sh
cd validation
cp .env.example .env   # 各 key に実際の port を書く
```

| key | 割り当てる board |
| --- | --- |
| `TEST_SERIAL_PORT_S3_HOST` / `TEST_SERIAL_PORT_PEER_DEVICE_S3_DEVICE` | S3 二台 |
| `TEST_SERIAL_PORT_P4_LOOPBACK` | loopback の旧 P4 |
| `TEST_SERIAL_PORT_P4_PSRAM` | 同じ loopback の旧 P4 |
| `TEST_SERIAL_PORT_P4_HS_HOST` / `TEST_SERIAL_PORT_PEER_DEVICE_P4_HS_DEVICE` | HS 直結の旧 P4 |
| `TEST_SERIAL_PORT_P4X_HS_HOST` / `TEST_SERIAL_PORT_PEER_DEVICE_P4X_HS_DEVICE` | HS 直結の P4X |
| `TEST_SERIAL_PORT_P4X_HS_PC` | Linux PC につないだ P4X |

- primary は `TEST_SERIAL_PORT_<PROFILE>`、peer device は `TEST_SERIAL_PORT_PEER_DEVICE_<PROFILE>` です。
- port は `/run/board-identify/by-id/...` のような安定した path を使います。
- 同じ物理 board を複数の profile に割り当ててよいのは、別の combo どうしの間だけです。一つの combo の中では使いません。
- `.env` は git の対象外です。

## bench

### 構成と結線

| 位置 | chip | PSRAM | 制御 / log | 結線 | combo |
| --- | --- | --- | --- | --- | --- |
| S3 二台 | ESP32-S3 | — | 各 board の書込み口 | native OTG どうしを FS で直結 | `s3` |
| 2-1 | ESP32-P4 v1.3（旧 P4） | 32 MiB | CH343 UART | 同じ board の HS 口と FS 口を結線（loopback） | `p4`、`p4_psram` |
| 2-3 | ESP32-P4 v1.3（旧 P4） | 32 MiB | CH340 UART | 2-4 と HS 口どうしを直結 | `p4_p4x_hs`、`p4x_p4_hs` |
| 2-4 | ESP32-P4 v3.2（P4X） | 32 MiB | USB Serial/JTAG | 2-3 と HS 直結 | `p4_p4x_hs`、`p4x_p4_hs` |
| 3-3 / 3-4 | ESP32-P4 v3.2（P4X） | 32 MiB | 3-3 の USB Serial/JTAG | 3-4 の HS 口を Linux PC へ直結 | `p4x_pc` |

位置 `N-M` はハブ N の port M です。設備の正本は bench の台帳（`oep-bench/hosts/native-linux/` の README と `inventory.json`）です。

- 書込み・log の port は、試験用の USB data 結線とは別です。試験 data を log の経路に流しません。
- 同じ board を使う組（`p4` と `p4_psram`、HS 直結の二方向）は同時に動かしません。実機の pytest は一つずつ実行します。
- 2-3 の HS 口から 2-4 へ電源が回り込みます。片方の電源を切るときは、両方の port をまとめて切ります。
- 機器の lock は plugin が取ります。
- S2 と S31 の組はありません。

### P4 loopback の注意

- `p4` / `p4_psram` は、一台の P4 の HS controller と FS controller をつなぎ、片方を Host、片方を Device にします。
- link は向きに関係なく **FS（12 Mbps、bulk MPS 64）** です。HS controller の Device 動作は確かめられますが、480 Mbps の試験にはなりません。
- 480 Mbps の HS は、HS 直結の combo（`p4_p4x_hs`、`p4x_p4_hs`、`p4x_pc`）で確かめます。

### Linux PC を Host にする試験

- `p4x_pc` の試験は、PC 側で pyusb（libusb）を使い、生の control / bulk 転送を組みます。sg3_utils や dfu-util は使いません。
- OS の class driver（usb-storage など）が先に bind する場合、試験側で明示的に detach します。
- 一般 user で開ける VID/PID は udev 規則で限られています（`1209:0001` など）。PC 試験の sketch はその VID/PID を使います。新しい VID/PID が要るときは、udev 規則の追加を依頼します。

## P4 と P4X の build 設定

製品の source に P4 と P4X を区別する処理はありません。違いは build 設定と SDK です。

| 項目 | 旧 P4（revision v1.x） | P4X（revision v3.x） |
| --- | --- | --- |
| FQBN | `esp32:esp32:esp32p4:ChipVariant=prev3,USBMode=default,CDCOnBoot=default` | `esp32:esp32:esp32p4:ChipVariant=postv3,USBMode=hwcdc,CDCOnBoot=cdc` |
| `Serial` の経路 | UART0 → USB-UART | USB Serial/JTAG（HWCDC） |
| library package | `esp32p4_es-libs` | `esp32p4-libs` |
| SDK が対応する revision | v1.x | v3.1 以上 |
| CPU clock（build 設定） | 360 MHz | 400 MHz |
| HS non-periodic / periodic TX queue depth | 4 / 4 | 8 / 16 |
| HS endpoint 数 / IN endpoint 数 / FIFO | 15 / 8 / 1024 word | 同じ |

- bench の P4X board は log を USB Serial/JTAG で出すので、`USBMode=hwcdc` にします。`USBMode=default` で build すると `Serial` が UART0 になり、制御用の port に log が出ません。
- `USBMode=hwcdc` は log の経路の選択です。試験対象は EspUsbDevice の HS controller で、USB Serial/JTAG とは別の端子です。
- P4X の SDK が対応するのは v3.1 以上です。v3.0 の実機は対応済みとして扱いません。
- endpoint 数と FIFO 容量は同じなので、製品側で分ける理由はありません。それでも旧 P4 の成功を P4X の合格にはせず、両方で試験します。
- examples の `esp32p4x` profile は `USBMode=default` です。bench の試験用 profile とは別です。

## PSRAM

- P4 系の bench board はすべて 32 MiB の PSRAM を持ちます（`validation/p4_hs/psram` で実測）。
- PSRAM を使う試験の profile は FQBN に `PSRAM=enabled` を足します（`p4_psram`、`peer/psram_link` の Device 側）。それ以外の P4 profile は PSRAM を有効にしません。
- 製品は、ring、MSC の保存先など CPU が読み書きする領域に PSRAM を使えます。USB の DMA が触る staging は内部 RAM のままです。
- 借用転送（`readDirect` / `writeDirect`）に PSRAM の buffer を渡すと `NotDmaCapable` で拒否します。試験もこの前提で書きます。

## HS で変わる値

HS 対応の controller では、bulk の MPS が 64 から 512 になります。試験の期待値も変わります。

| 項目 | FS | HS |
| --- | --- | --- |
| bulk MPS | 64 | 512 |
| MSC `transferBytes` | 64 の倍数 | 512 の倍数（それ以外は `begin()` が `ESP_ERR_INVALID_SIZE`） |
| CCID の既定 `messageBytes` | 271 | 512 |
| 借用受信の容量 | 64 の倍数 | 512 の倍数 |
| 非 control IN endpoint の上限 | 4 | 7 |
| HID interrupt endpoint | 64 byte まで | 1024 byte まで |
| UAC2 feedback | 10.14 を 3 byte | 16.16 を 4 byte |

- HS 対応の controller を選ぶと、link が FS でも bulk の容量を 512 で検査します。P4 の loopback で MSC の staging 64 が拒否されるのはこのためです。
- CDC / Vendor / MIDI の `transferBytes` は MPS 未満でも受け付けます。
- `begin()` は構成全体の IN endpoint が TX FIFO に入るかを検査し、入らなければ `ESP_ERR_INVALID_SIZE` です。P4 の HS controller では、既定の Vendor は 5 個まで入り、6 個目で失敗します。
- link の速度は、Device 側では `device.speed()` で読めます。test の期待値は `"_hs_" in request.node.name` で決めます。

Host 側（ESP-IDF の USB Host）の制約も試験の組み方に影響します。

- bulk IN の `num_bytes` は MPS の倍数が必要です。
- `usb_host_endpoint_clear` は Host 側の data toggle を戻しません。HALT の試験は、clear 前の packet 数が偶数になるように組みます。
- HS の HID では、Host の周期 OUT FIFO の既定（512 byte）が足りない場合があります。
- Vendor IN は、公開 EspUsbHost の cache 準備済み read queue と callback で受けます。同期の `vendorReadSync()` は P4 で cache の問題があるため使いません。

## 実行方法

### 準備

```sh
cd validation
uv sync --locked
cp .env.example .env   # port を設定する
```

### 収集と全件実行

```sh
uv run --locked --env-file .env pytest --combo all --collect-only -q
uv run --locked --env-file .env pytest --combo all -q -rs --junitxml=.pytest-results/v3-all.xml
```

- 件数は試験の追加で変わります。毎回 `--collect-only` で収集数とエラー 0 を確かめます。
- 全件を最後まで流すときは `--maxfail=1000` を足します。

### 一部だけ実行する

```sh
uv run --locked --env-file .env pytest --combo s3 -q -rs                              # 一つの combo
uv run --locked --env-file .env pytest peer/vendor_link --combo p4_p4x_hs -q -rs      # 一つの probe
uv run --locked --env-file .env pytest peer --combo p4_p4x_hs --combo p4x_p4_hs -q -rs
uv run --locked --env-file .env pytest p4/psram --combo p4_psram -q -rs
uv run --locked --env-file .env pytest --combo all --run-mode=build -q                # build だけ
```

### build probe

`validation/build_probes/` は、未使用の機能がリンクされないことを確かめる build 専用の sketch です。pytest では実行しません。

```sh
arduino-cli compile --profile s3_device validation/build_probes/include_only
```

- `no_library` と `include_only` の Flash / RAM が同じなら、include だけの費用は 0 です。
- `device_only`、`vendor_copy`、`cdc_copy`、`midi_packet`、`msc_only`、`audio_only`、`video_only`、`ncm_only`、`dfu_only` は、選んだ class 以外の symbol が ELF に残らないことを `nm` で確かめます。
- `controller_storage` は DWC2 の転送 record などの開始時領域を確かめます。profile は `s2_device` と `p4_device` です。

### offline の確認

```sh
python3 tools/verify_usb_backend.py
```

保守対象の DWC2 file があること、旧 vendor の path・設定 header・`tusb*.h` の include がないことを確かめます。
CI（`.github/workflows/v3-checks.yml`）は、これと `pytest --combo all --collect-only` を実行します。

### 結果の見方

- 成果物は `validation/.pytest-results/`（git 対象外）に置きます。
- 終了値 0 だけを合格にしません。収集数、JUnit の passed / skipped / failed / error、未完了の数、各 serial log が停止したこと（panic がないこと）を確かめます。
- serial の原本は plugin の出力先（`/tmp/pytest-embedded/`）にあります。最新の directory ではなく、JUnit の時刻から該当するものを探します。
- `--maxfail=1` で途中終了すると、後続は未実行です。未実行と合格を区別します。

## 公開 API の契約を確かめる probe

文書に書いた API の約束は、次の probe で wire の上から確かめます。

| probe | combo | 確かめること |
| --- | --- | --- |
| `s3/api_semantics` | `s3`、`p4_p4x_hs`、`p4x_p4_hs` | Keyboard の press の足し込み、修飾キーの bit、release 後の詰め、tap が足した分だけ離すこと、`releaseKey()`、7 つ目のキーの拒否。Mouse の `move()` が button を保つことと範囲外の拒否。HID の packet 長（`inputPacketSize()`、`EspUsbDeviceHidCustom` の `inputReportSize` 200 が FS 64 / HS 200）。WebUSB / MS OS 2.0 と同じ bRequest のほかの vendor 要求が sketch に届くこと。callback が戻らないときの `end()` の `ESP_ERR_TIMEOUT` と再呼出し。TEST_MODE が HS だけで受け付けられること |
| `s3/vendor_packet_size` | `s3`、`p4_p4x_hs`、`p4x_p4_hs` | Vendor の FS bulk 長 8 / 16 / 32 / 64 が動き、それ以外を `begin()` が拒否すること。HS は常に 512 |
| `peer/fifo_budget` | `p4_p4x_hs`、`p4x_p4_hs` | HS で既定の Vendor 5 個は列挙でき、6 個目は `begin()` が `ESP_ERR_INVALID_SIZE` |
| `p4x_pc/msc_fat` | `p4x_pc` | `EspUsbDeviceMscFatRamDisk` の 1 / 4 / 24 MiB の volume を PC が読み、`fsck.fat` で検査する |

## 新しい probe を足すとき

- primary の Host と `peer_device/` の Device という標準の構造を使い、test 関数は `dut` と `peers["device"]` を受け取ります。
- combo 名は Host を先に書きます。
- FS 前提の値（MPS 64、FS の FIFO、ISO の interval、endpoint 数）は、速度と能力から期待値を決めます。
- HS の試験で FS に fallback したら失敗にします。性能や機能を同じに見せるための黙った fallback はしません。
- NCM は同じ MCU 内の lwIP 通信で合格にせず、raw frame と USB の転送量で wire 経由を確かめます。
- DFU は RAM backend で確かめます。実際の OTA / SD への書込みは別の試験範囲です。
- 試験の目的は動作と契約の確認です。旧版との benchmark や最大帯域は優先しません。

## 確認済みの範囲

| 範囲 | 確認したこと |
| --- | --- |
| S3 ↔ S3（FS） | `s3/` の全 probe。10 class、lifecycle、確保失敗からの回復、取消し・reset・end の競合、標準要求、suspend / resume、remote wakeup |
| 旧 P4 loopback（FS、両 controller） | 10 class の基本通信、65537 byte までの借用 DMA、再開始。PSRAM 上の MSC 保存先と Vendor ring |
| 旧 P4 ↔ P4X の HS 直結（両方向） | `s3/` の全 probe と `peer/` の HS 専用 probe。HS の列挙、MPS 512、借用転送、Audio / Video の ISO、PSRAM 上の保存先 |
| P4X → Linux PC（HS） | MSC の媒体交換（06/28/00、02/3A/00、residue）、DFU DETACH |
| build | S2 / S3 / P4 / P4X の compile。未使用 class がリンクされないこと |

## 未確認の範囲

| 範囲 | 状態 |
| --- | --- |
| ESP32-S2 の実機 | compile のみ。実機の組がない |
| ESP32-S31 | 未対応 |
| Windows / macOS | class の bind、各 class の動作とも未確認 |
| Linux PC の class driver | 上記の MSC / DFU の wire 検査だけ。CDC / HID / Audio / Video / NCM の OS driver との組合せは未確認 |
| SD card と OTA の実媒体 | RAM backend の試験だけ |
| 長時間の連続運転 | 回数を決めた反復（再接続を含む）まで |
| 実ケーブルの抜去による VBUS 検出（`vbusSensing=true`） | 未確認 |
| USB DMA が PSRAM を直接読み書きする構成 | 製品が拒否するので対象外 |
| Arduino-ESP32 4 系 | 未確認。基準は 3.3.12 |
| 任意の競合順序（SETUP / reset / end / SET_CONFIGURATION の全組合せ） | 代表的な順序だけ |

残作業は [TODO](../../TODO.ja.md) にあります。
