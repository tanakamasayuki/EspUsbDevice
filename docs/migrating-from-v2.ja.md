# EspUsbDevice 2.x からの移行

> English: [migrating-from-v2.md](migrating-from-v2.md)

EspUsbDevice 2.x（最後の版は 2.6.1）で書いたスケッチを 3.0 でビルドするための案内です。
多くのスケッチは形が変わりません。`EspUsbDevice` を一つ、Function ごとに object を一つ置き、
`device.begin(config)` を一度呼びます。
変わるのは次の三つです。buffer の大きさの決め方、callback を登録できる時期、
そして USB と各 class の規格に合わせて直した動作です。

Arduino-ESP32 コアの USB API（`USB.begin()`、`USBHIDKeyboard` など）から移る場合は、
[Arduino-ESP32 の USB API からの移行](migrating-from-arduino-esp32-usb.ja.md)を見てください。

## 目次

1. [まず読む](#1-まず読む)
2. [移行の手順](#2-移行の手順)
3. [共通の変更](#3-共通の変更)
4. [class ごとの変更](#4-class-ごとの変更)
5. [削除・改名された API](#5-削除改名された-api)
6. [例の対応表](#6-例の対応表)
7. [まだ確認していないこと](#7-まだ確認していないこと)

## 1. まず読む

### 何が変わったか

1. **build flag はありません。** `build_opt.h`、`CFG_TUD_*`、`ESP_USB_DEVICE_CCID_BUFFER_SIZE` は廃止です。
   各 Function の buffer は、実行時にその `configure()` で決めます。
   v3 は TinyUSB ではなく、ライブラリ自身の USB スタックを使います。
2. **資源は `begin()` が確保し、`end()` が解放します。** リンクされるのは作った Function だけです。
   `end()` → `configure()` → `begin()` で、再ビルドせずに大きさを変えられます。
3. **callback は停止中に登録します。** `on...()` の登録関数はすべて `esp_err_t` を返します。
   `device.begin()` の後に呼ぶと `ESP_ERR_INVALID_STATE` で失敗します。
4. **開始と停止がはっきりしました。** `device.task()` は廃止です。Function 自身の `begin()` は非公開になりました。
   状態は `started()`、`ready()`、`suspended()`、`speed()` で分かります。`end()` は `esp_err_t` を返します。
5. **`begin()` が構成全体を先に検査します。** endpoint と FIFO の予算、packet の大きさ、class の設定を、
   PHY を動かす前に確かめます。動かない構成は、Host での列挙の失敗ではなく `begin()` の失敗になります。
6. **compile error にならない動作の変更があります。** Keyboard のキーが重なる、`mouse.move()` が押下中のボタンを保つ、
   System Control が標準の usage ID を使う、CDC の `write()`/`flush()` が待つ、MIDI が範囲外の値を拒否する、などです。
   [コンパイルでは分からない変更](#コンパイルでは分からない変更)を見てください。
7. **借用転送は共通の API になりました。** `writeDirect()` / `readDirect()` は `EspUsbTransferSubmission` を返し、
   完了は型付きの通知で一度だけ返ります。65535 byte の上限はありません。

### 移行しないとどうなるか

| v2 のコード | v3 での結果 |
| --- | --- |
| `CFG_TUD_CDC_RX_BUFSIZE`、`CFG_TUD_HID_EP_BUFSIZE` などを書いた `build_opt.h` | compile error：`Remove TinyUSB build flags; configure EspUsbDevice/Function runtime APIs instead` |
| `-DESP_USB_DEVICE_CCID_BUFFER_SIZE=...` | `EspUsbCcidConfig::messageBytes` を示す compile error |
| `device.task()`、`config.startTinyUsb`、`EspUsbDevice::maxCdcPorts()`、`device.hidEndpointSize()` | compile error（廃止） |
| `cdc.begin()`、`keyboard.begin()` など Function の `begin()` | compile error（非公開） |
| `EspUsbDeviceVideo::payloadBufferSize()` などの static な照会 | compile error（instance ごとになった） |
| `dfu.onError([](uint8_t s) {...})` | compile error（引数は `EspUsbDfuStatus`） |
| `#include <tusb.h>`、`tud_*()` の呼出し | compile error（v3 は TinyUSB を含まない） |
| `device.begin()` の後に登録した callback | compile は通る。登録関数が `ESP_ERR_INVALID_STATE` を返し、callback は呼ばれない |
| [下の表](#コンパイルでは分からない変更)の動作の変更 | compile は通る。device の動作が変わる |

v2 と、v2 が同梱していた TinyUSB の `CFG_TUD_*` / `CFG_TUSB_*` の flag と、v2 の容量の flag
（`ESP_USB_DEVICE_CDC_PORTS`、`ESP_USB_DEVICE_HID_EP_BUFSIZE`、`ESP_USB_TINYUSB_AUDIO_*_EP_SIZE`）は、
どれも build が error で止まります。`build_opt.h` は書き換えずに、ファイルごと削除してください。

### コンパイルでは分からない変更

次のものはそのまま compile でき、動作だけが変わります。
スケッチや Host 側のソフトが関わるものを一つずつ確かめてください。詳しくは 4 章にあります。

| 対象 | v2 | v3 |
| --- | --- | --- |
| `device.begin()` の後の callback 登録 | 有効になった | `ESP_ERR_INVALID_STATE` で拒否。呼ばれない |
| Keyboard の `pressUsage()` / `pressKey()`（6KRO） | 押下中のキーを置き換えた | 押下中のキーに足す（6 個まで） |
| Keyboard の `tapKey()` / `tapUsage()` | すべてのキーを離した | tap で足したものだけを離す |
| `mouse.move(x, y)` | 押下中のボタンを離した | 押下中のボタンを保つ |
| System Control | 1 / 2 / 3 | 0x81 / 0x82 / 0x83。整数 1〜3 は拒否 |
| Gamepad の hat | 中央を Host が方向として読んだ | 中央は null 状態 |
| Consumer Control の usage > 0x3FF | 送った | 拒否 |
| HID Vendor の `sendInput()` が長すぎる | 切り詰めた | 拒否 |
| CDC の `write()` / `flush()` | すぐ戻った | 最大 1000 ms 待つ（`setTxTimeoutMs()`） |
| Vendor で packet 長の倍数を送った後の ZLP | 送った | 既定では送らない |
| bRequest 0x01 の Vendor 要求 | WebUSB 有効時はライブラリが取った | WebUSB の GET_URL だけを取る。残りはスケッチに届く |
| scheme のない WebUSB の URL | `https://` とみなした | URL 全体として送る。ブラウザは拒否する |
| MIDI の範囲外の channel / data | 丸めて送った | 拒否 |
| MSC の callback が要求より少なく返す | 再試行・継続した | 媒体エラー |
| MSC の媒体の挿し直し | 次の command は成功した | UNIT ATTENTION（06/28/00）を一度返す |
| MSC の既定の staging | 4096 byte | 512 byte |
| SD card の `readOnly(true)` | 書込みを媒体エラーにした | 書込み保護として報告する |
| Audio の `capture.write()` が満杯のとき | 古い PCM を上書きした | 部分書込み |
| UAC2 の FS feedback | 4 byte（16.16） | 3 byte（10.14） |
| CCID の FS での既定の message 長 | 320 | 271 |
| NCM の既定の MAC | chip の Ethernet MAC | 同じ値で locally administered bit を立てたもの |
| P4 の既定値（Vendor の送信、Video の payload） | P4 では大きかった | すべての chip で同じ |

### v3 で得られるもの

ESP32-S3、Arduino-ESP32 3.3.12 で、v2.6.1 と v3 の同じ機能の例を比べた実測です。

- **静的 RAM：** 約 59.5 KB → 約 22 KB。
  v2 は、スケッチが使わない class の buffer もリンク時に確保していました。v3 は作った Function だけをリンクします。
  ただし v3 は、その buffer を `begin()` で heap から確保します。`begin()` の後の空き heap も確かめてください。
- **flash：** 1.6〜10 % 減。

実装上の事実として、ほかに次のものがあります。

- Function の数に上限はありません（v2 は 6 個）。controller の endpoint と FIFO で決まり、
  収まらなければ `begin()` が `ESP_ERR_INVALID_SIZE` で知らせます。
- ESP32-P4 の HS（480 Mbps）に対応します。P4 の revision v1.x と v3.x（P4X）の両方です。
  `device.speed()` で Host と決まった速度が分かります。
- 各 Function が失敗の理由を返します（`startError()`）。Device 全体は `device.lastError()` / `lastErrorName()` です。
- 大きさは instance ごとです。CDC を二つ、Vendor を二つ置いて、別の大きさにできます。
- ring をスケッチから渡せます。PSRAM も使えます。
- CDC は Arduino の `Stream` です（`peek()`、`readBytes()`、`setTimeout()`）。
- `DescriptorsOnly` の開始方式で、USB の hardware を動かさずに descriptor を作って検査できます。
- remote wakeup（`remoteWakeupSupported`、`device.remoteWakeup()`）と、bus の休止の状態（`device.suspended()`）。
- 規格に合わせた修正：FS の bulk packet 長（USB 2.0 §5.8.3）、UAC2 の FS feedback の形式（USB 2.0 §5.12.4.2）、
  gamepad の hat の null 状態、System Control の usage、読み取り専用 SD の書込み保護の報告、
  自分の要求だけを処理する WebUSB / Microsoft OS 2.0。

## 2. 移行の手順

上から順に進めてください。各段階は前の段階を前提にします。

1. **`build_opt.h` を削除します。** 設定していた値は控えておきます。必要なものは次の段階で `configure()` に移します。
2. **大きさを `configure()` に移します。** `setup()` の中で、`device.begin()` より前に、各 Function の `configure()` を呼びます。
   今の設定から始めると、変えたい項目だけを変えられます。

   ```cpp
   EspUsbBulkStreamConfig c = cdc.configuration();
   c.rxCapacity = 4096;          // 旧 CFG_TUD_CDC_RX_BUFSIZE
   if (cdc.configure(c) != ESP_OK) { /* 表示して止める */ }
   ```

   class ごとの対応は [4 章](#4-class-ごとの変更)にあります。
3. **callback は `device.begin()` の前に登録し、結果を確かめます。**

   ```cpp
   if (cdc.onLineState(handleLineState) != ESP_OK) { /* 表示する */ }
   ```

4. **開始と停止を直します。**
   - `device.task()` を消します。
   - Function 自身の `begin()`（`cdc.begin()`、`keyboard.begin()` など）の呼出しを消します。
     `msc.begin(blockCount, blockSize)` は残します。これは容量を決めるだけです。
   - `config.startTinyUsb = false` は `config.startMode = EspUsbDeviceStartMode::DescriptorsOnly` にします。
   - 送信は `device.ready()` が `true` の間だけ行います。
   - device を止めるなら、`device.end()` の `esp_err_t` を確かめます。
5. **class ごとの変更を当てます**（[4 章](#4-class-ごとの変更)）。
   compile が通っても、[コンパイルでは分からない変更](#コンパイルでは分からない変更)は必ず読んでください。
6. **失敗を表示します。** `begin()` が `false` なら `device.lastErrorName()` と、Function の `startError()` を出します。

   ```cpp
   if (!device.begin(config)) {
     Serial.printf("begin failed: %s (cdc: %s)\n", device.lastErrorName(),
                   esp_err_to_name(cdc.startError()));
   }
   ```

7. **Host で確かめます。** 試験中は PID か serial 番号を変え、Host が v2 の firmware について覚えた情報を使わないようにします。
   descriptor を比べ（Linux なら `lsusb -v`）、スケッチが出す機能をすべて試します。
   立上げの手順は [USB デバイス開発ガイド](usb-device-guide.ja.md)にあります。

## 3. 共通の変更

### Device の設定

`EspUsbDeviceConfig` の v2 の項目は残っています（文字列、VID/PID、`deviceVersion`、`selfPowered`、
`maxPowerMilliamps`、WebUSB、Microsoft OS 2.0 の項目、`deviceInterfaceGuid`、`controller`、`bulkInBuffering`、`taskCoreId`）。
変わったものは次のとおりです。

| v2 | v3 | 備考 |
| --- | --- | --- |
| `startTinyUsb = true` | `startMode = EspUsbDeviceStartMode::Usb`（既定） | |
| `startTinyUsb = false` | `startMode = EspUsbDeviceStartMode::DescriptorsOnly` | descriptor を作り資源を確保する。PHY、割込み、worker は使わない。`ready()` は `false` のまま |
| `-DCFG_TUD_VBUS_DETECT_HW` | `vbusSensing = true` | VBUS を chip へ配線した board だけ |
| 固定の worker task | `taskStackBytes`（4096）、`taskPriority`（-1 = 最高）、`eventQueueDepth`（16） | callback はこの task の stack で動く |
| `end()` は無期限に待つ | `stopTimeoutMs`（5000。0 で無期限） | 期限切れは `ESP_ERR_TIMEOUT`。もう一度 `end()` を呼ぶ |
| `webusbUrl = "example.com/x"` | `webusbUrl = "https://example.com/x"` | 下を参照 |
| - | `remoteWakeupSupported` | remote wakeup を広告する。使うかどうかは Host が決める |

文字列は UTF-8 で、UTF-16 にして 126 文字までです。不正な UTF-8 や長すぎる文字列は、切り詰めずに `begin()` が失敗します。

**WebUSB の URL。** v2 は scheme のない URL を `https://` とみなしていました。v3 は書いたとおりに送ります。
`http://` も `https://` もないと、URL descriptor は scheme 255（「文字列が URL 全体」）になり、
ブラウザは `example.com/x` を拒否します。scheme を書いてください。

### 開始・停止と状態

```cpp
// v2
void loop() {
  device.task();
  if (device.ready()) keyboard.tapKey('a');
}
```

```cpp
// v3
void loop() {
  if (device.ready() && !device.suspended()) keyboard.tapKey('a');
}
```

| 照会 | `true` になる条件 |
| --- | --- |
| `started()` | `begin()` が成功し、`end()` をまだ呼んでいない。reset 中や抜いている間も `true` |
| `ready()` | Host が device を構成した（SET_CONFIGURATION） |
| `suspended()` | bus が休止している。`ready()` は `true` のまま |
| `speed()` | `EspUsbBusSpeed::None` / `Full` / `High` |

- `begin()` は検査、確保、開始を行います。`end()` は worker と DMA を止め、借用 buffer を返し、memory を解放します。
- callback の中で `begin()`、`end()`、`configure()` を呼ばないでください。USB worker からの `end()` は `ESP_ERR_INVALID_STATE` です。
- Function の destructor と `end()` は、device が止まるまで待ちます。
- `ready() && !suspended()` でも、次の転送の成功は約束されません。送信ごとに戻り値を確かめてください。

### callback

- 登録関数はすべて `esp_err_t` を返します。停止中は `ESP_OK`、開始後は `ESP_ERR_INVALID_STATE` です。
  `device.onAnyControlRequest()` も同じです。
- callback は USB worker task で動きます。短くしてください。止まって待つ、転送の完了を待つ、大量に表示する、はしないでください。
  callback では flag を立てるかデータを写すだけにし、処理は `loop()` で行う形がよく使われます。
- worker の stack は `taskStackBytes`（既定 4096）です。callback に足りなければ増やします。

### エラーの読み方

- `device.begin()` は `bool` を返します。`false` なら `device.lastError()` / `lastErrorName()` が理由です。
  `lastErrorName()` は `esp_err_to_name()` の文字列を返します。
- 各 Function には、`begin()` のうち自分の部分の結果を返す `startError()` があります。
- `configure()` はその場で `esp_err_t` を返します。

| コード | よくある意味 |
| --- | --- |
| `ESP_ERR_INVALID_ARG` | 設定が不正、または食い違っている（例：format のない Audio stream） |
| `ESP_ERR_INVALID_SIZE` | 大きさが収まらない：endpoint や FIFO の予算、packet 長、必要より小さい buffer |
| `ESP_ERR_INVALID_STATE` | 呼ぶ時期が違う：開始後の callback 登録、callback からの `end()` |
| `ESP_ERR_NO_MEM` | heap の確保に失敗した |
| `ESP_ERR_NOT_SUPPORTED` | その chip や方式ではできない |
| `ESP_ERR_TIMEOUT` | 待ちの期限が切れた（HID の送信、`end()`） |

症状と原因は[トラブルシューティング](troubleshooting.ja.md)にあります。

### ESP32-P4 の controller 選択と HS

- `config.controller` は v2 にもあり、意味は同じです。`Auto`（P4 は HS、S2/S3 は FS）、`FullSpeed`、`HighSpeed`。
  TinyUSB の port 番号（rhport 0/1）に頼っていた v2 のスケッチは、それが要らなくなります。
- v3 は P4 の revision v1.x と v3.x（P4X）に対応します。Arduino の board 設定の `ChipVariant` を chip に合わせてください（`prev3` か `postv3`）。
- HS では bulk packet が 512 byte になります。FS で動いた設定でも、HS では `begin()` が失敗することがあります。

| 設定 | HS での条件 |
| --- | --- |
| MSC の `transferBytes` | 512 の倍数（既定の 512 はよい。64 や 256 は失敗） |
| CCID の `messageBytes` | 0（自動）か 512 以上 |
| `readDirect()` の容量 | 512 の倍数 |
| control 以外の IN endpoint | P4 の HS controller は 7 本（S2/S3 と P4 の FS controller は 4 本） |

- v2 の P4 の既定値には、v3 より大きいものがありました。Vendor の送信 4096 byte（4096 byte ずつの転送）、
  HID Vendor の packet 最大 512、Video の payload 1023 です。v3 はどの chip でも同じ既定値です。
  以前の速度が必要なら `configure()` で設定してください。

### ビルドの設定

- 基準のコアは Arduino-ESP32 3.3.12 です。
- ESP32-S3 は *USB Mode* を "USB-OTG (TinyUSB)"（`USBMode=default`）にします。
- *USB CDC On Boot*、*USB MSC On Boot*、*USB DFU On Boot* は無効のままにします。
- `build_opt.h` を削除します。このライブラリのためだけに置いたファイルなら、ファイルごと消します。
  Arduino は前のビルド結果を残すので、一度 cache を消してビルドします（`arduino-cli compile --clean`）。

### 自作の Function class

`EspUsbDeviceClass` から派生した class は、名前の変更だけでは済みません。
descriptor を書く関数の約束、開始・停止の hook、HID の基底 class が変わりました。
HID の class は `EspUsbHidFunction` から派生します。[発展的な使い方](usb-device-advanced.ja.md)を見てください。

## 4. class ごとの変更

### 既定の容量

| Function | v2 の既定 | v3 の既定（`configure()`） |
| --- | --- | --- |
| CDC | 受信 512、送信 512 | `rxCapacity` 512、`txCapacity` 512、`transferBytes` 512 |
| Vendor | 受信 512、送信 512（P4 は送信 4096、転送 4096） | 512、512、`transferBytes` 512 |
| MIDI | 各方向 512 byte | `rxPackets` 128、`txPackets` 128（512 byte） |
| MSC | staging 4096 byte | `transferBytes` 512 |
| DFU | block 1024 byte | `transferBytes` 1024 |
| CCID | message 320 byte | `messageBytes` 0 = `max(271, Bulk-OUT MPS)`：FS は 271、HS は 512 |
| Video | payload 512（P4 は 1023） | `payloadBytes` 512 |
| NCM | 3200 byte の NTB を送信 3・受信 2 | 同じ：`txBuffers` 3、`rxBuffers` 2、`ntbInBytes`/`ntbOutBytes` 3200 |

ring を大きくすると heap を使い、余裕が増えます。MSC の既定は v2 より小さいので、
v2 と同じ速度が要るなら `transferBytes = 4096` にします。

### HID

```cpp
// v2（build_opt.h: -DCFG_TUD_HID_EP_BUFSIZE=512）
EspUsbDeviceHidVendor hid(device, 256);
hid.onOutputReport(handleOutput);
```

```cpp
// v3：flag は不要。report は 65534 byte まで
EspUsbDeviceHidVendor hid(device, 256);
if (hid.onOutputReport(handleOutput) != ESP_OK) { /* device.begin() より前に呼ぶ */ }
```

- `CFG_TUD_HID*` は廃止です。report の領域は `begin()` で report descriptor から決まります。
- `onOutputReport()`、`onFeatureReport()`、`onProtocol()`、`enableNkro()` は `esp_err_t` を返し、停止中だけ成功します。
- `device.hidEndpointSize()` は、Function ごとの `inputPacketSize()` になりました（Host と決まった packet 長。bus reset の前は 0）。
- 新しい API：`inputReportCapacity()`、`outputReportCapacity()`、`featureReportCapacity()`、`setFeatureReport()`、
  `compositeReportId()`、`keyboard.releaseKey(char)`。
- `EspUsbDeviceHidCustom` の `inputReportSize` は interrupt IN の packet 長を決めます（FS は最大 64、HS は最大 1024）。
  長い report は複数の packet で送ります。
- 複合 HID の interface は、各 Function が希望する packet 長の最大を使います（NKRO は 32、Custom/Vendor は report 長）。
  v2 は 16 に固定していました。

動作の変更（compile error にならない）：

| | v2 | v3 |
| --- | --- | --- |
| NKRO なしの Keyboard の `pressUsage()` / `pressKey()` | 押下中のキーと modifier を置き換えた | 押下中のキーに足す。6 個まで。7 個目は `ESP_ERR_INVALID_SIZE` で、状態は変わらない。modifier は OR で重ねる |
| modifier の usage 0xE0〜0xE7 | キーの配列に入れた | modifier の bit を立てる |
| `tapUsage()` / `tapKey()` | 最後に `releaseAll()` した | tap で足したものだけを離す |
| `pressUsage(u, m, x)` の第 3 引数 | `holdMs`（使われていなかった） | `timeoutMs`（既定 100） |
| NKRO 有効時の `pressKey()` | boot report を送った | NKRO の report を使う |
| layout で打てない文字 | `false` | `false`。`lastError` は `ESP_ERR_NOT_SUPPORTED` |
| `mouse.move(x, y)` | 押下中のボタンを離した（`buttons` の既定 0） | 押下中のボタンを保つ（`KeepButtons`）。0〜255 を渡すとその値にする |
| System Control の値 | `POWER_OFF` = 1、`STANDBY` = 2、`WAKE_HOST` = 3 | 0x81、0x82、0x83（Generic Desktop の usage ID）。整数 1/2/3 は拒否。`usage()` は 0x81〜0x83 を返す |
| Consumer Control の usage > 0x3FF | 送った | `ESP_ERR_INVALID_ARG` で拒否 |
| Gamepad の hat | logical 0〜8。中央（0）が方向として読まれた | logical 1〜8 と null 状態。`HAT_CENTER`（0）は「方向なし」。定数と report の長さは同じ |
| HID Vendor の `sendInput()` が `reportSize()` より長い | `reportSize()` に切り詰めた | `false` を返す |

System Control と hat の変更は HID Usage Tables に合わせたものです（Generic Desktop の 0x81〜0x83、null 状態を持つ Hat Switch）。
古い hat の値を補正していた Host 側のソフトは確かめ直してください。

```cpp
// v2 でも v3 でも：定数はそのまま動く
system.click(ESP_USB_DEVICE_SYSTEM_CONTROL_STANDBY);
// v2 だけ：整数 1〜3
system.click(2);   // v3 では拒否。定数を使う
```

[HID](classes/hid.ja.md)を参照してください。

### CDC

```cpp
// v2（build_opt.h: -DCFG_TUD_CDC_RX_BUFSIZE=4096）
EspUsbDeviceCdcSerial cdc(device);
void setup() {
  device.begin();
  cdc.onLineState(handleLineState);   // begin の後：v2 では有効だった
}
```

```cpp
// v3
EspUsbDeviceCdcSerial cdc(device);
void setup() {
  EspUsbBulkStreamConfig c = cdc.configuration();
  c.rxCapacity = 4096;
  if (cdc.configure(c) != ESP_OK || cdc.onLineState(handleLineState) != ESP_OK) return;
  device.begin();
}
```

- `CFG_TUD_CDC_*` は `configure(EspUsbBulkStreamConfig)` になりました：`rxCapacity`、`txCapacity`、`transferBytes`、
  `rxMode`、任意の外部 `rxBuffer` / `txBuffer`。
- CDC は Arduino の `Stream` です。`peek()`、`readBytes()`、`setTimeout()` が使え、`Stream&` として渡せます。
- `onLineCoding()`、`onLineState()`、`onRx()`、`setName()` は `esp_err_t` を返します。
- `lineCoding()` / `lineState()` は参照ではなく値を返します。
- `port()` は CDC の descriptor 順の番号です。v2 では TinyUSB の instance 番号も兼ねていましたが、その意味はなくなりました。
- `EspUsbDevice::maxCdcPorts()` は廃止です。`begin()` が構成全体を検査します。
  失敗したら `lastError()` を、成功したら `readCapacity()` / `writeCapacity()` を見ます。
- 新しい API：`onBreak()`、`setTxTimeoutMs()`、`peek()`、借用転送。

動作の変更：**`write()` と `flush()` が待ちます。**
送信 ring が満杯なら、`write()` / `print()` は空きができるまで TX timeout（既定 1000 ms）まで待ちます。
`flush()` は、データと必要な ZLP を送り終えるまで待ちます。
v2 は入った分だけですぐ戻り、`flush()` は送信を始めるだけでした。
止まった terminal で `loop()` を止めたくないなら、期限を短くします。

```cpp
cdc.setTxTimeoutMs(100);   // 0 で v2 に近い動作：すぐ戻る
```

USB worker からの呼出しは待ちません。[CDC](classes/cdc.ja.md)を参照してください。

### Vendor

```cpp
// v2
vendor.onRx([](size_t n) {
  uint8_t buf[64];
  size_t got = vendor.read(buf, min(n, sizeof(buf)));
  vendor.write(buf, got);
  vendor.flush();                       // 短い返事には必要だった
});
```

```cpp
// v3
volatile bool rxPending = false;
// setup() で device.begin() より前に
vendor.onRx([](size_t) { rxPending = true; });

void loop() {
  uint8_t buf[64];
  size_t got = vendor.read(buf, sizeof(buf));
  if (got && vendor.waitWritable(got)) vendor.write(buf, got);   // autoFlush が送る
}
```

- `CFG_TUD_VENDOR_*` は `configure(EspUsbVendorConfig)` になりました（CDC と同じ struct）。
- `autoFlush` が既定で有効なので、返事のたびの `flush()` は要りません。`flush()` は送信を始めるだけで、待ちません。
- `onRx()` は通知です。`loop()` で読むか、`rxMode = EspUsbBulkRxMode::Callback` と `onRxData()` で、
  worker 上で packet ごとに受け取ります。
- `writeCapacity()` は instance ごとです（v2 は static）。`waitWritable()` と `writeAvailable()` は残っています。
- コンストラクタの `endpointSize` は FS の bulk packet 長です。8、16、32、64 のどれかにします（64 より大きい値は 64 になります）。
  それ以外は `begin()` が `ESP_ERR_INVALID_SIZE` で失敗します（USB 2.0 §5.8.3）。HS は常に 512 です。

借用転送：

| v2 | v3 |
| --- | --- |
| `-DCFG_TUD_VENDOR_TXRX_BUFFERED=0` が必要 | 常に使える |
| `bool writeDirect(buf, len)` | `EspUsbTransferSubmission writeDirect(buf, len, termination)`。`if (!vendor.writeDirect(...))` はそのまま compile できる。`bool ok = vendor.writeDirect(...)` はできない（明示的な変換） |
| `lastDirectError()`、`EspUsbDeviceVendorDirectError` | `submission.error`（`EspUsbTransferError`） |
| 完了は `onTxComplete(size_t)` | `onDirectTxComplete(const EspUsbTransferResult &)`。`begin()` 前の登録が必須。成功、取消し、bus reset、停止のどの結果でも buffer を返す |
| 65535 byte の上限 | 固定の上限なし（`maxDirectTransfer()` は表せる最大で、空き memory ではない） |
| - | `readDirect()`、`cancelDirectTx()` / `cancelDirectRx()` |

buffer は 64 byte 境界にそろえ、DMA できる内部 RAM に置きます。PSRAM は `NotDmaCapable` で拒否されます。

control 要求：

- ライブラリが処理するのは、WebUSB の GET_URL（bRequest 0x01、bmRequestType 0xC0、wIndex 2。WebUSB 有効時）と、
  Microsoft OS 2.0 descriptor の要求（bRequest 0x02、0xC0、wIndex 7）にぴったり合うものだけです。
  ほかの要求はすべて `onControlRequest()` に届きます。v2 は、WebUSB が有効なら bRequest 0x01 をすべて取っていました。
- データ段階のある OUT 要求には受信先の buffer が要ります。
  SETUP 段階で buffer を渡し、DATA 段階で `actualBytes` を確かめ、ACK 段階で反映します。

```cpp
// v2：status だけを返し、データ段階は受け取っていなかった
return vendor.sendControlResponse(r);
```

```cpp
// v3
uint8_t cmd[64];   // 転送が終わるまで有効にしておく
switch (r.stage) {
case ESP_USB_DEVICE_CONTROL_STAGE_SETUP: return vendor.sendControlResponse(r, cmd, r.wLength);
case ESP_USB_DEVICE_CONTROL_STAGE_DATA:  return r.actualBytes == r.wLength;
default:                                 apply(cmd, r.actualBytes); return true;
}
```

`wLength` が 0 なら、`sendControlResponse(r)` は以前と同じく status 段階だけを送ります。

動作の変更：**ZLP を自動では送りません。**
v2 は、長さが packet 長の倍数の転送の後に長さ 0 の packet を送っていました。
v3 の Vendor の stream は送りません（`streamTermination` の既定は `Continue`）。
短い packet で読み終わりを決める Host は、続きのデータを待ち続けます。
メッセージに長さを付けるか、`streamTermination = EspUsbStreamTermination::EndWithShortPacket` にしてください。
[Vendor](classes/vendor.ja.md)を参照してください。

### MIDI

```cpp
// v2（build_opt.h: -DCFG_TUD_MIDI_RX_BUFSIZE=1024）
EspUsbDeviceMidi midi(device, 2);
```

```cpp
// v3
EspUsbDeviceMidi midi(device, 2);
void setup() {
  EspUsbMidiConfig c = midi.configuration();   // cable 数 2 を保つ
  c.rxPackets = 256;
  c.name = "My MIDI";
  if (midi.configure(c) != ESP_OK) return;
  device.begin();
}
```

- `CFG_TUD_MIDI_*` は `configure(EspUsbMidiConfig)` になりました：`inCables`、`outCables`、`rxPackets`、`txPackets`、
  `transferPackets`、`rxMode`、`name`。
- `EspUsbMidiConfig` は cable 数も持ちます。既定の 0 は「今の数を保つ」なので、新しく作った config を
  渡しても、コンストラクタで指定した cable 数は保たれます。
- cable 数は 1〜16 です。v2 は 17 以上を丸めていましたが、v3 は拒否します。
- MIDI は `Print` ではありません。v2 と同じく、`readPacket()` / `writePacket()` とメッセージ用の関数を使います。
- 新しい API：`onPacket()`（`rxMode = Callback` と組み合わせる）、`receiveErrors()`、`receiveCapacity()`、`sendCapacity()`、`startError()`。

動作の変更：channel > 15、data byte > 127、pitch bend > 16383 は拒否され（`false`、`ESP_ERR_INVALID_ARG`）、何も送りません。
v2 は値を丸めて送っていました。
受信では、未知の cable や予約された CIN の packet を捨て、`receiveErrors()` に数えます。[MIDI](classes/midi.ja.md)を参照してください。

### MSC と helper

```cpp
// v2（build_opt.h: -DCFG_TUD_MSC_EP_BUFSIZE=4096）
msc.mediaPresent(true);
disk.attach(msc);
device.begin();
```

```cpp
// v3
EspUsbMscConfig c;
c.transferBytes = 4096;          // 512 の倍数：FS でも HS でも有効
c.name = "RAM disk";
if (msc.configure(c) != ESP_OK) return;
if (!disk.attach(msc)) return;
msc.mediaPresent(true);
device.begin();
```

- `CFG_TUD_MSC_EP_BUFSIZE` は `EspUsbMscConfig::transferBytes` になりました（既定 512。64 の倍数で、
  bulk packet 長の倍数であること。HS では 512 の倍数）。
- `onRead()`、`onWrite()`、`onStartStop()`、`vendorID()`、`productID()`、`productRevision()` は `esp_err_t` を返し、停止中だけ成功します。
- `msc.begin(blockCount, blockSize)` は今も容量を決めます。先に `onRead()` を登録してください。ないと `false` を返します。
- `RamDisk::attach()` と `FatRamDisk::attach()` は、今も媒体の有無と書込み可否を変えません。`msc.mediaPresent(true)` は自分で呼びます。
  `FirmwareDisk::attach()` は今も媒体ありにします。
- `SdCard::attach()` は、device が動いている間は失敗するようになりました。

動作の変更：

| | v2 | v3 |
| --- | --- | --- |
| read/write の callback が要求より少なく返す、または後で再試行するため 0 を返す | TinyUSB が再試行・継続した | 媒体エラー（sense 03/11/00）。全量を同期して返す |
| 構成済みの間に `mediaPresent(false)` → `true` | 次の command は成功した | 次の command（INQUIRY と REQUEST SENSE を除く）を一度だけ UNIT ATTENTION 06/28/00 で失敗させる（SPC の規定） |
| READ/WRITE の途中で `mediaPresent(false)` | - | その command の残りを 02/3A/00 で失敗させる |
| `SdCard::readOnly(true)` | 書込みを Medium Error にした | 書込み保護として報告（MODE SENSE の WP）。書込みは 07/27/00 で失敗 |
| 約 2 MiB を超える `FatRamDisk` | 不正な FAT12（cluster が多すぎる） | 容量に応じて cluster を大きくし、常に正しい FAT12 |
| 既定の staging | 4096 byte | 512 byte |

[MSC](classes/msc.ja.md)を参照してください。

### Firmware 更新と DFU

`EspUsbDeviceFirmwareUpdate`、`EspUsbDeviceMscFirmwareDisk`、`EspUsbDeviceMscSdCard` の API は変わりません。

```cpp
// v2（build_opt.h: -DCFG_TUD_DFU_XFER_BUFSIZE=4096）
dfu.onError([](uint8_t status) { Serial.println(status); });
runtime.onDetach([]() { startMyUpdate(); });   // 既定の再起動を置き換えた
```

```cpp
// v3
EspUsbDfuConfig c = dfu.configuration();
c.transferBytes = 4096;
dfu.configure(c);
dfu.onError([](EspUsbDfuStatus status) { lastStatus = unsigned(status); });
runtime.setDetachHandler([]() { startMyUpdate(); });
runtime.onDetach([]() { return !busy; });        // 任意：false で DETACH を STALL
```

- `CFG_TUD_DFU_XFER_BUFSIZE` は `EspUsbDfuConfig::transferBytes` になりました。
- `transferSize()` は instance ごとです（v2 は static）。
- `onError()` の引数は `EspUsbDfuStatus` です。`unsigned(status)` で v2 と同じ番号になります。
- Runtime：v2 の `onDetach(void())` は、ROM loader への既定の再起動を置き換えていました。v3 ではそれが `setDetachHandler()` です。
  `onDetach()` は、SETUP の時点で DETACH を受け付けるかを `bool` で返すものになりました。
- `onProgress()`、`onComplete()`、`onError()`、`restartWhenComplete()` は `esp_err_t` を返し、停止中だけ成功します。
- 新しい API：内蔵の OTA 以外へ書くための `setBackend()`。

[DFU](classes/dfu.ja.md) と [USB 経由の OTA](ota-over-usb.ja.md)を参照してください。

### Audio

```cpp
// v2（build flag で FIFO の大きさを決めた）
playback.addFormat({48000, 2, 2, 16});
device.begin(config);
```

```cpp
// v3
if (!playback.addFormat({48000, 2, 2, 16})) return;
EspUsbAudioConfig c;
c.playbackBufferFrames = 480;    // 48 kHz で 10 ms
c.eventCapacity = 16;
if (audio.configure(c) != ESP_OK) return;
device.begin(config);
```

- Audio の build flag は `EspUsbAudioConfig` になりました：`eventCapacity`、`playbackBufferFrames`、`captureBufferFrames`、
  任意の `playbackBuffer` / `captureBuffer` とその byte 長、`playbackOverflowPolicy`。
- 食い違った設定（format のない stream、PCM の形ごとに違う rate の組、一方向の中で変わる channel 数）は、
  `begin()` が `ESP_ERR_INVALID_ARG` で失敗します。buffer が足りないときは `ESP_ERR_INVALID_SIZE` です。
- 新しい API：`clearFormats()`、`currentFormat()`、`capacity()`、`packetCapacity()`、`capture.availableForWrite()`。

動作の変更：

| | v2 | v3 |
| --- | --- | --- |
| 満杯のときの `capture.write()` | すべて受け取り、古い PCM を上書きした | 部分書込み：入った量を返し、上書きしない。受け取るのは frame 単位だけ |
| playback の ring が満杯 | - | 既定は `DropOldest`（新しい frame を残す）。未読の PCM を残すなら `DropNewest` |
| UAC2 の FS の feedback endpoint | 4 byte、16.16 | 3 byte、10.14（USB 2.0 §5.12.4.2）。HS は 4 byte、16.16 のまま |

capture のコードは、`availableForWrite()` の分だけ書くか、入らなかった残りを保持してください。
[Audio](classes/audio.ja.md)を参照してください。

### Video

```cpp
// v2（build_opt.h: -DCFG_TUD_VIDEO_STREAMING_BULK=1）
camera.onFrameComplete([]() { frameDone = true; });
Serial.println(EspUsbDeviceVideo::payloadBufferSize());
```

```cpp
// v3
EspUsbVideoConfig c;
c.transport = EspUsbVideoTransport::Bulk;
c.payloadBytes = 1024;
camera.configure(c);
camera.onFrameResult([](const EspUsbTransferResult &r) { frameDone = true; });
device.begin(config);
Serial.println(camera.payloadBufferSize());   // instance ごと。begin() 前は 0
```

- `CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE` / `_BULK` は `EspUsbVideoConfig` になりました：`transport`、`payloadBytes`、
  `isochronousPacketBytes`、`payloadBuffer`。
- `payloadBufferSize()`、`isochronousPacketSize()`、`bulkStreaming()` は instance ごとです（v2 は static）。
- `sendFrame()` と `onFrameComplete()` は残っています。`submitFrame()` は拒否の理由を含む `EspUsbTransferSubmission` を返し、
  `onFrameResult()` は取り消したものや中断したものも含め、すべての frame を返します。`cancelFrame()` も新しく加わりました。
- `onFrameComplete()`、`onCommit()` は `esp_err_t` を返します。

[Video](classes/video.ja.md)を参照してください。

### CCID

```cpp
// v2（build_opt.h: -DESP_USB_DEVICE_CCID_BUFFER_SIZE=600）
reader.onApdu(handleApdu);
```

```cpp
// v3
EspUsbCcidConfig c;
c.messageBytes = 600;            // 0 = 自動
reader.configure(c);
if (reader.onApdu(handleApdu) != ESP_OK) return;
```

- `ESP_USB_DEVICE_CCID_BUFFER_SIZE` は `EspUsbCcidConfig::messageBytes` になりました。
  既定の 0 は `max(271, Bulk-OUT MPS)` を選びます（FS は 271、HS は 512）。
  CCID 1.1 は少なくとも Bulk-OUT の packet 長を求めます。v2 の 320 では P4 の HS port で開始できませんでした。
  FS の既定は 271（短い APDU 一つ分）になったので、それより長い応答を返すなら大きな値を指定します。
  決まった値は `messageCapacity()` で分かります。
- `removable = false` にすると interrupt endpoint を省きます。その場合、カードは `begin()` の前に挿入します。
- `onApdu()`、`onEscape()`、`onPower()`、`removeCard()` は `esp_err_t` を返します。

[CCID](classes/ccid.ja.md)を参照してください。

### NCM（USB ネットワーク）

- 固定の NTB pool は `EspUsbNetConfig` になりました：`ntbInBytes`、`ntbOutBytes`、`txBuffers`、`rxBuffers`、
  `maxDatagrams`、`maxFrameBytes`、任意の buffer、`name`。
- `macAddress()`、`onFrame()`、`ipConfig()`、`dhcpServer()`、`dhcpClient()`、`dhcpAdvertiseGateway()`、`dhcpDns()`、
  `defaultRoute()` は `esp_err_t` を返します。動作中は MAC や設定を変えられません。
- 新しい API：`endNetwork()`、`stats()`、容量の照会。

```cpp
// v2
net.dhcpServer(true);
```

```cpp
// v3
if (net.dhcpServer(true) != ESP_OK) { /* 表示する */ }
```

動作の変更：既定の MAC は、chip の Ethernet MAC に locally administered bit を立て、multicast bit を消したものです。
v2 は chip の Ethernet MAC をそのまま使っていたので、既定の値が変わります。
MAC に結び付けた Host 側の設定（固定 address、firewall の規則など）は作り直しが必要なことがあります。
値を固定したいなら `macAddress()` を明示してください。[NCM](classes/ncm.ja.md)を参照してください。

## 5. 削除・改名された API

| v2 | v3 | 備考 |
| --- | --- | --- |
| `build_opt.h`、`CFG_TUD_*` | 各 Function の `configure()` | 一覧にある flag は compile error |
| `ESP_USB_DEVICE_CCID_BUFFER_SIZE` | `EspUsbCcidConfig::messageBytes` | compile error |
| `#include <tusb.h>`、`tud_*()` | Function と Device の API | 例：`tud_speed_get()` → `device.speed()` |
| `device.task()` | （削除する） | USB worker がすべて処理する |
| `config.startTinyUsb` | `config.startMode` | `Usb` / `DescriptorsOnly` |
| `void end()` | `esp_err_t end()` | |
| `EspUsbDevice::maxCdcPorts()` | （廃止） | `begin()` が構成を検査する |
| `device.hidEndpointSize()` | `hid.inputPacketSize()` | Function ごと |
| Function の `begin()` / `afterDeviceStarted()` | （protected） | `device.begin()` / `end()` を使う。`msc.begin(count, size)` は残る |
| `void on...()` の登録関数 | `esp_err_t on...()` | 停止中だけ |
| `void onAnyControlRequest()` | `esp_err_t onAnyControlRequest()` | 停止中だけ |
| `EspUsbDeviceClass::isHid()` の既定 `true` | 既定 `false` | 自作 class だけに関係 |
| `keyboard.pressUsage(u, m, holdMs)` | `pressUsage(u, m, timeoutMs)` | |
| `mouse.move(..., uint8_t buttons = 0)` | `move(..., int16_t buttons = KeepButtons)` | |
| `ESP_USB_DEVICE_SYSTEM_CONTROL_*` = 1/2/3 | = 0x81/0x82/0x83 | 名前は同じ |
| `const &` を返す `cdc.lineCoding()` | 値を返す | `lineState()` も同じ |
| `cdc.setName()`（`void`） | `esp_err_t setName()` | |
| `EspUsbDeviceVendor::writeCapacity()`（static） | instance ごと | |
| `vendor.writeDirect()` → `bool` | → `EspUsbTransferSubmission` | |
| `vendor.lastDirectError()`、`lastDirectErrorName()` | `submission.error` | |
| `EspUsbDeviceVendorDirectError` | `EspUsbTransferError` | |
| `-DCFG_TUD_VENDOR_TXRX_BUFFERED=0` | （不要） | 残すと compile error。削除する |
| `msc.vendorID()` など（`void`） | `esp_err_t` | |
| `EspUsbDeviceDfu::transferSize()`（static） | instance ごと | |
| `dfu.onDetach(void())` | `setDetachHandler(void())` | 受付の判断は `onDetach(bool())` |
| `dfu.onError(void(uint8_t))` | `onError(void(EspUsbDfuStatus))` | |
| `EspUsbDeviceVideo::payloadBufferSize()`（static） | instance ごと | `isochronousPacketSize()`、`bulkStreaming()` も同じ |
| `camera.onFrameComplete()` だけ | `onFrameResult()`、`submitFrame()`、`cancelFrame()` も | |
| `reader.removeCard()`（`void`） | `esp_err_t` | |
| `net.ipConfig()` など（`void`） | `esp_err_t` | |
| - | `device.started()`、`suspended()`、`speed()`、`remoteWakeup()` | 新規 |
| - | `EspUsbDeviceConfig::taskStackBytes`、`taskPriority`、`eventQueueDepth`、`vbusSensing`、`stopTimeoutMs`、`remoteWakeupSupported` | 新規 |

## 6. 例の対応表

v2 のすべての例に、同じ名前と場所の v3 の例があります。35 例と `Info` の下の 3 sketch で、合わせて 38 sketch です。
v3 の各例の README には「v2 からの変更点」の節があり、その sketch で変えたことを書いています。

| v2 の例 | v3 の例 | 主な移行の点 |
| --- | --- | --- |
| Keyboard | [Keyboard](../examples/Keyboard/README.ja.md) | `onOutputReport()` が `esp_err_t` を返す。キーが重なる |
| KeyboardMouse | [KeyboardMouse](../examples/KeyboardMouse/README.ja.md) | `compositeReportId()`。複合の packet 長 |
| KeyboardNKRO | [KeyboardNKRO](../examples/KeyboardNKRO/README.ja.md) | `enableNkro()` / `onProtocol()` が `esp_err_t` を返す |
| Mouse | [Mouse](../examples/Mouse/README.ja.md) | `move()` が押下中のボタンを保つ |
| Gamepad | [Gamepad](../examples/Gamepad/README.ja.md) | hat の null 状態 |
| MediaKeys | [MediaKeys](../examples/MediaKeys/README.ja.md) | System Control の usage ID |
| CustomHID | [CustomHID](../examples/CustomHID/README.ja.md) | `inputReportSize` は packet 長 |
| VendorHID | [VendorHID](../examples/VendorHID/README.ja.md) | `CFG_TUD_HID_EP_BUFSIZE` は不要。`sendInput()` は切り詰めない |
| Serial | [Serial](../examples/Serial/README.ja.md) | `configure()`、`onBreak()`、TX timeout |
| SerialMulti | [SerialMulti](../examples/SerialMulti/README.ja.md) | `maxCdcPorts()` は廃止。port ごとの大きさ |
| USBVendor | [USBVendor](../examples/USBVendor/README.ja.md) | 要求の番号、OUT のデータ段階、`https://` |
| MIDI | [MIDI](../examples/MIDI/README.ja.md) | `configure()`、`receiveErrors()` |
| MIDIController | [MIDIController](../examples/MIDIController/README.ja.md) | 拒否された送信をやり直す。callback で受信 |
| MIDIInterface | [MIDIInterface](../examples/MIDIInterface/README.ja.md) | 拒否されたメッセージを保持して再送 |
| MSC | [MSC](../examples/MSC/README.ja.md) | `transferBytes`。挿し直しの UNIT ATTENTION |
| MSCFatRamDisk | [MSCFatRamDisk](../examples/MSCFatRamDisk/README.ja.md) | `configure()`。取り出しは `loop()` で処理 |
| MSCSdCard | [MSCSdCard](../examples/MSCSdCard/README.ja.md) | 4 KiB の staging。書込み保護 |
| CompositeHidCdcMsc | [CompositeHidCdcMsc](../examples/CompositeHidCdcMsc/README.ja.md) | Function ごとの `configure()`。CDC が待つ |
| AudioSpeaker | [AudioSpeaker](../examples/AudioSpeaker/README.ja.md) | `EspUsbAudioConfig`。`DropOldest` |
| AudioMicrophone | [AudioMicrophone](../examples/AudioMicrophone/README.ja.md) | 部分書込みの `write()` |
| AudioHeadset | [AudioHeadset](../examples/AudioHeadset/README.ja.md) | 部分書込みの `write()` |
| AudioSpeakerM5 | [AudioSpeakerM5](../examples/AudioSpeakerM5/README.ja.md) | 同上（M5Stack 用） |
| AudioMicrophoneM5 | [AudioMicrophoneM5](../examples/AudioMicrophoneM5/README.ja.md) | 同上（M5Stack 用） |
| AudioHeadsetM5 | [AudioHeadsetM5](../examples/AudioHeadsetM5/README.ja.md) | 同上（M5Stack 用） |
| VideoCamera | [VideoCamera](../examples/VideoCamera/README.ja.md) | `EspUsbVideoConfig`。`onFrameResult()` |
| SmartCardReader | [SmartCardReader](../examples/SmartCardReader/README.ja.md) | `messageBytes`。登録関数が `esp_err_t` を返す |
| UsbNetwork | [UsbNetwork](../examples/UsbNetwork/README.ja.md) | `EspUsbNetConfig`。setter が `esp_err_t` を返す |
| FirmwareDFU | [FirmwareDFU](../examples/FirmwareDFU/README.ja.md) | `transferBytes`。`EspUsbDfuStatus` |
| FirmwareBootMode | [FirmwareBootMode](../examples/FirmwareBootMode/README.ja.md) | `setDetachHandler()` / `onDetach(bool)` |
| FirmwareCDC | [FirmwareCDC](../examples/FirmwareCDC/README.ja.md) | `flush()` が返事の送信を待つ |
| FirmwareMSC | [FirmwareMSC](../examples/FirmwareMSC/README.ja.md) | `transferBytes`。UF2 |
| FirmwareVendor | [FirmwareVendor](../examples/FirmwareVendor/README.ja.md) | `configure()`。status の byte |
| FirmwareHTTP | [FirmwareHTTP](../examples/FirmwareHTTP/README.ja.md) | `EspUsbNetConfig`。setter が `esp_err_t` を返す |
| P4FullSpeedDevice | [P4FullSpeedDevice](../examples/P4FullSpeedDevice/README.ja.md) | `controller`、`vbusSensing`、`ChipVariant` |
| P4HighSpeedDevice | [P4HighSpeedDevice](../examples/P4HighSpeedDevice/README.ja.md) | HS での大きさの条件 |
| Info/EspUsbDeviceBringUpCheck | [Info/EspUsbDeviceBringUpCheck](../examples/Info/EspUsbDeviceBringUpCheck/README.ja.md) | |
| Info/EspUsbDeviceConsole | [Info/EspUsbDeviceConsole](../examples/Info/EspUsbDeviceConsole/README.ja.md) | |
| Info/EspUsbDeviceDescriptorDump | [Info/EspUsbDeviceDescriptorDump](../examples/Info/EspUsbDeviceDescriptorDump/README.ja.md) | |

v3 だけにある例：

| 例 | 示すもの |
| --- | --- |
| [CdcConsole](../examples/CdcConsole/CdcConsole.ino) | CDC を `Stream&` として渡す |
| [MidiEcho](../examples/MidiEcho/MidiEcho.ino) | MIDI packet の echo |
| [VendorEcho](../examples/VendorEcho/VendorEcho.ino) | 書ききれなかった残りを保持する Vendor の echo |
| [AudioPlayback](../examples/AudioPlayback/AudioPlayback.ino) | 複数の format を持つ UAC2 の playback |
| [MscCdcBlocks](../examples/MscCdcBlocks/MscCdcBlocks.ino) | MSC の raw block と CDC |
| [CompositeCdcMidi](../examples/CompositeCdcMidi/CompositeCdcMidi.ino) | 大きさを別々に決めた CDC と MIDI |
| [ReconfigureVendor](../examples/ReconfigureVendor/ReconfigureVendor.ino) | 実行中の `end()` → `configure()` → `begin()` |

## 7. まだ確認していないこと

実機の試験は次の組合せで行っています。ESP32-S3 二台の FS の Host/device、ESP32-P4 一台の HS↔FS の loopback、
旧 P4 と P4X の HS 直結（両方向）、P4X と Linux PC の HS 接続です。

まだ確認していないもの：

- ESP32-S2 の実機（compile だけ）。
- Windows と macOS の Host。
- 実物の SD card での MSC、実機での OTA の切替えと再起動。
- 多くの例の実機での動作。各例の README に、確かめた範囲を書いています。

これらに頼る前に、自分の hardware で確かめてください。
