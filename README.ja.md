# EspUsbDevice

> English: [README.md](README.md)

EspUsbDevice は、どんな USB device にするかをスケッチで明示する、ESP32 Arduino 向けの
USB Device ライブラリです。使う Function を Device に付け、容量を決めて `begin()` します。
USB の処理はライブラリの worker が行うので、スケッチから USB の task を呼ぶ必要はありません。
リンクされるのは、使う Function だけです。

```cpp
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceCdcSerial console(device);   // USB serial port と
EspUsbDeviceHidKeyboard keyboard(device); // keyboard を 1 つの device に

void setup() {
  EspUsbBulkStreamConfig buffers;
  buffers.rxCapacity = 256;
  buffers.txCapacity = 1024;
  console.configure(buffers);
  if (!device.begin()) {
    // 理由は device.lastErrorName() で分かる（例："ESP_ERR_INVALID_SIZE"）
  }
}

void loop() {
  if (device.ready() && console.available()) {
    const int c = console.read();
    console.write(uint8_t(c));
    if (c == '!') keyboard.tapKey('a');
  }
  delay(1);
}
```

はじめて使うときは[サンプルの進め方](docs/example-workflow.ja.md)、目的から探すときは
[サンプル一覧](examples/README.ja.md)を見てください。動かないときは、まず
[Info](examples/Info/) の bring-up check を動かします。

Arduino-ESP32 標準の `USB`、`USBHIDKeyboard`、`USBCDC`、`USBMSC` などとの互換は目標にしません。
標準 API から移る場合は[移行ガイド](docs/migrating-from-arduino-esp32-usb.ja.md)が対応表になっています。

## 対応 chip と class

| chip | 速度 | [HID](docs/classes/hid.ja.md) | [CDC](docs/classes/cdc.ja.md) | [Vendor](docs/classes/vendor.ja.md) | [MIDI](docs/classes/midi.ja.md) | [MSC](docs/classes/msc.ja.md) | [Audio](docs/classes/audio.ja.md) | [Video](docs/classes/video.ja.md) | [NCM](docs/classes/ncm.ja.md) | [CCID](docs/classes/ccid.ja.md) | [DFU](docs/classes/dfu.ja.md) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| ESP32-S3 | FS（12 Mbps） | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| ESP32-P4 v1.x と P4X（v3.1 以降） | HS（480 Mbps）と FS | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| ESP32-S2 | FS（12 Mbps） | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

✅ = `tests/` の試験で、実機の転送を確かめています。ESP32-S3 二台の FS、ESP32-S2 と S3 を FS で
直結した両方向、P4 と P4X を HS で直結した両方向、P4 一台の HS と FS controller の結線、P4X と S2 の
それぞれと Linux PC です。ESP32-S31 にはまだ対応していません。

class の一覧：

- **HID：** keyboard（6 キーか NKRO、配列の変換、LED 状態）、mouse、gamepad、Consumer Control（メディアキー）、
  System Control、vendor 定義の report、スケッチが書く report descriptor
- **CDC ACM：** USB serial port。Arduino の `Stream` で、複数の port を置ける
- **Vendor：** bulk の送受信と vendor の control 要求。WebUSB と Microsoft OS 2.0（WinUSB）の descriptor を生成する
- **MIDI 1.0：** 方向ごとに最大 16 cable
- **MSC：** RAM・FAT12 RAM・SD card・firmware の disk を helper で公開する
- **Audio：** UAC1 と UAC2 の再生と録音
- **Video：** UVC のカメラ（MJPEG か YUY2、Isochronous か Bulk）
- **NCM：** CDC-NCM の USB ネットワーク。任意で lwIP と DHCP
- **CCID：** スマートカードリーダー。カードの中身をスケッチで書く
- **DFU 1.1：** `dfu-util` での firmware 更新と runtime の DETACH

自由に組み合わせて 1 つの複合 device にできます。上限は controller の endpoint と FIFO だけで、
収まらない構成は `begin()` が拒否します。

## 必要なもの

- Arduino-ESP32 **3.3.12**（例の `sketch.yaml` で固定しています）。
- ESP32-S3 と ESP32-S2：**ツール > USB Mode** を **USB-OTG (TinyUSB)**（`USBMode=default`）にします。
  ESP32-S3 の既定は Hardware CDC and JTAG です。
- ESP32-P4：**ツール > Chip Variant** で revision を選びます。v1.x は `prev3`、v3.1 以降（P4X）は `postv3` です。
- core 自身の USB CDC・MSC・DFU を起動時に有効にしたり、`USB.begin()` を呼んだりしないでください。
  USB OTG controller はこのライブラリが使います。

## 3.0 で変わったこと

3.0 はメジャー版です。2.x のスケッチには変更が要ります。手順は [v2 からの移行](docs/migrating-from-v2.ja.md)にあります。

### TinyUSB から、ライブラリ自身の USB スタックへ

2.x は、ライブラリが同梱した TinyUSB を、ライブラリの `tusb_config.h` で build していました。
class の buffer の大きさは `CFG_TUD_*` の build flag で決まり、変えるには `build_opt.h` を書いて
全体を build し直す必要がありました。使わない class の buffer もリンク時に確保されていました。

3.0 は TinyUSB の class と共通 stack を使いません。USB の処理はすべてライブラリ自身のコードです。

```text
スケッチ
  ↓
EspUsbDevice と Function（HID, CDC, Vendor, MIDI, MSC, Audio, Video, NCM, CCID, DFU）
  ↓
descriptor の生成、endpoint の割当て、転送の契約
  ↓
core（EP0、標準要求、event queue）と runtime（PHY、controller の選択、USB worker task）
  ↓
DWC2 driver（register、FIFO、DMA、割込み、cache）
  ↓
ESP-IDF（PHY、FreeRTOS、heap）
```

- class driver と共通 core は、USB 2.0 と各 class の規格に合わせて新しく書いたものです。
- controller の DWC2 driver だけは TinyUSB（MIT）と ST（BSD-3-Clause）のコードに由来し、ライブラリの一部として保守しています。
  由来と変更の範囲は [third_party/tinyusb](third_party/tinyusb/PROVENANCE.ja.md) にあります。
- `#include <tusb.h>`、`tud_*()` の呼出し、`CFG_TUD_*` の flag は使えません。旧 flag を定義すると compile error になり、代わりの API を示します。
- Arduino-ESP32 は、SoC・PHY・FreeRTOS を使うための platform として引き続き使います。core の USB Device 実装（`USB.h` 系）とは同時に使えません。

### 使い方の変化

| 項目 | 2.x | 3.0 |
| --- | --- | --- |
| buffer の大きさ | `build_opt.h` の `CFG_TUD_*`。全体を build し直す | 各 Function の `configure()` で実行時に決める。instance ごとに違ってよい |
| メモリの確保 | 使わない class の分もリンク時に静的に確保 | `begin()` が使う Function の分だけ確保し、`end()` が解放する |
| 大きさの変更 | build し直す | `end()` → `configure()` → `begin()` |
| USB の task | `device.task()` を `loop()` で呼ぶ | ライブラリの worker task が動く。`device.task()` は廃止 |
| Function の開始 | `keyboard.begin()` などを個別に呼べた | `device.begin()` だけ。Function の `begin()` は非公開 |
| callback の登録 | いつでも | `device.begin()` の前だけ。登録関数は `esp_err_t` を返す |
| 状態 | `ready()` | `started()`、`ready()`、`suspended()`、`speed()` |
| 失敗の知り方 | Host での列挙の失敗として現れる | `begin()` が USB を動かす前に検査し、`lastErrorName()` と各 Function の `startError()` が理由を示す |
| Function の数 | 6 個まで | 上限なし。controller の endpoint と FIFO で決まる |
| ESP32-P4 | FS と HS の port 番号を意識する | `config.controller` で選ぶ。`Auto` は HS。revision v1.x と P4X の両方 |
| 大きなデータの転送 | 65535 byte まで | 借用 DMA 転送（`writeDirect` / `readDirect`）。完了は一度だけ通知される |
| ring の置き場所 | 内部 RAM の固定領域 | スケッチが渡せる。PSRAM も可 |

実測（ESP32-S3、同じ機能の例どうし）では、静的 RAM が約 59.5 KB から約 22 KB に減り、flash は 1.6〜10 % 減りました。
3.0 の buffer は `begin()` で heap から確保するので、`begin()` の後の空き heap も確かめてください。

### compile は通るが動作が変わるもの

次のものはそのまま compile でき、動作だけが変わります。多くは規格に合わせた修正です。

- `device.begin()` の後に登録した callback は呼ばれません（登録関数が `ESP_ERR_INVALID_STATE` を返す）。
- Keyboard の `pressUsage()` は押下中のキーに足します（6 個まで）。`tapKey()` は自分が足したキーだけを離します。
- `mouse.move()` は押しているボタンを保ちます。
- System Control は標準の usage 0x81〜0x83 で指定します。範囲外の MIDI や Consumer の値は丸めずに拒否します。
- CDC の `write()` / `flush()` は、送れるまで最大 1000 ms 待ちます（`setTxTimeoutMs()`）。
- MSC の媒体を挿し直すと、次の command に UNIT ATTENTION を一度返します。既定の staging は 512 byte です。
- UAC2 の FS feedback は 3 byte（10.14）です。scheme のない WebUSB の URL は、書いたとおりの文字列として送ります。

全体の一覧は [v2 からの移行の「コンパイルでは分からない変更」](docs/migrating-from-v2.ja.md#コンパイルでは分からない変更)にあります。

## 基本の形

```cpp
#include <EspUsbDevice.h>

EspUsbDevice device;                       // Device を先に宣言する
EspUsbDeviceVendor vendor(device);         // Function は Device に自分を登録する

void setup() {
  Serial.begin(115200);

  EspUsbVendorConfig buffers = vendor.configuration();   // 今の設定から始める
  buffers.rxCapacity = 4096;
  if (vendor.configure(buffers) != ESP_OK) return;       // 停止中だけ成功する

  vendor.onRx([](size_t) { /* USB worker で呼ばれる。短く */ });   // callback は begin の前に

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4002;
  config.product = "My device";
  if (!device.begin(config)) {
    Serial.printf("begin: %s (vendor: %s)\n", device.lastErrorName(),
                  esp_err_to_name(vendor.startError()));
  }
}

void loop() {
  if (!device.ready() || device.suspended()) return;   // Host が構成するまで待つ
  uint8_t data[64];
  const size_t n = vendor.read(data, sizeof(data));
  if (n) vendor.write(data, n);                        // 受け付けた byte 数を返す
}
```

- **順序：** 宣言 → `configure()` → callback の登録 → `device.begin()`。変えるときは `device.end()` してから。
- **状態：** `started()` は `begin()` が成功してから `end()` まで、`ready()` は Host が構成した（SET_CONFIGURATION）とき、
  `suspended()` は bus が休止しているときに `true` です。`speed()` は Host と決まった速度を返します。
- **callback：** すべて USB worker task で動きます。待つ処理、大量の表示、`begin()` / `end()` は書かず、
  flag を立てるかデータを写して、処理は `loop()` で行います。worker の stack は `config.taskStackBytes`（既定 4096）です。
- **エラー：** `begin()` は `bool` を返し、理由は `device.lastError()` / `lastErrorName()` です。
  `ESP_ERR_INVALID_SIZE` は endpoint や FIFO に収まらない構成、`ESP_ERR_INVALID_ARG` は設定の誤り、
  `ESP_ERR_INVALID_STATE` は呼ぶ時期の誤りです。症状からは[トラブルシューティング](docs/troubleshooting.ja.md)を引けます。
- **事前の検査：** `config.startMode = EspUsbDeviceStartMode::DescriptorsOnly` にすると、USB を動かさずに
  descriptor を作って構成を検査できます。[Info](examples/Info/) の DescriptorDump が使います。
- **remote wakeup：** `config.remoteWakeupSupported = true` で広告し、Host が許可していれば `device.remoteWakeup()` で起こせます。

## class ごとの要点

各 class の全設定、動作、Host OS 側の注意は [class の文書](docs/classes/)にあります。

### HID

```cpp
EspUsbDeviceHidKeyboard keyboard(device);
EspUsbDeviceHidMouse mouse(device);
// loop() の中で
keyboard.write("hello\n");   // 文字列をキー操作として送る（setLayout() で配列を選ぶ）
keyboard.tapUsage(0x28);     // usage ID で送る（Enter）
mouse.move(10, 0);
```

- 文字の helper（`write`、`tapKey`、`pressKey`）と、usage ID・raw report の API の両方があります。
- `keyboard.enableNkro(true)`（`begin()` の前）で N-key rollover。Host が boot protocol を選んでいる間（BIOS など）は 6 キーの形式で送ります。
- `keyboard.ledState()` は Host が送った LED の状態（Num / Caps / Scroll Lock など）を値で返します。
- Gamepad、Consumer Control、System Control、vendor 定義の report（`EspUsbDeviceHidVendor`）、
  自分で書いた report descriptor（`EspUsbDeviceHidCustom`）を同じ device に並べられます。
  ESP32-P4 の HS では、1 packet 1024 byte までの interrupt endpoint を使えます。

### CDC（USB serial）

- `EspUsbDeviceCdcSerial` は Arduino の `Stream` です。`print()`、`readBytes()`、`setTimeout()` が使えます。
- 名前を付けて複数置けます（`EspUsbDeviceCdcSerial(device, "Console")`）。1 port で IN endpoint を 2 本使います。
- line coding、DTR / RTS、break を callback と照会で受け取れます。

### Vendor、WebUSB、WinUSB

- bulk の送受信は、コピー（`read` / `write`）、callback（`onRxData`）、借用 DMA（`writeDirect` / `readDirect`）から選べます。
- 借用 DMA は、スケッチの buffer をコピーせずに DMA へ渡します。完了は `EspUsbTransferResult` で一度だけ返り、
  取消し、bus reset、`end()` のときも buffer は必ず戻ります。
- vendor の control 要求をスケッチで処理できます。WebUSB の landing page と、Windows が driver を自動で当てる
  Microsoft OS 2.0 descriptor を、実際に割り当てた interface に合わせて生成します。

### MIDI

```cpp
EspUsbDeviceMidi midi(device);              // cable 1 本
midi.noteOn(0, 60, 100);                     // channel 0〜15、note、velocity
EspUsbDeviceMidiPacket packet;
while (midi.readPacket(packet)) { /* 受信 */ }
```

- `EspUsbDeviceMidi(device, inCables, outCables)` で、方向ごとに 16 本までの cable を Host に見せます。

### MSC（USB ドライブ）

```cpp
EspUsbDeviceMsc msc(device);
uint8_t storage[32 * 512];
EspUsbDeviceMscRamDisk volume(storage, 32);
// setup() で
volume.attach(msc);
msc.mediaPresent(true);
```

- BOT と SCSI はライブラリが処理し、block の読み書きは callback か helper に任せます。
- helper：`EspUsbDeviceMscRamDisk`（raw block）、`EspUsbDeviceMscFatRamDisk`（ファイルを置ける FAT12。容量に合わせて cluster を選ぶ）、
  `EspUsbDeviceMscSdCard`（SD card）、`EspUsbDeviceMscFirmwareDisk`（ファイルを書くと firmware を更新する）。
- 保存先は PSRAM に置けます。媒体の取り外しと交換を Host に正しく伝えます。

### Audio

```cpp
EspUsbAudioFunction audio(device, EspUsbAudioProtocol::Uac2);   // 省略すると UAC1
EspUsbAudioPlaybackStream &playback = audio.addPlaybackStream();
// setup() で
playback.addFormat({48000, 2, 2, 16});   // rate、channel、byte/sample、bit/sample
// loop() で
size_t n = playback.read(pcm, sizeof(pcm));
```

- Playback（PC → ESP32）と Capture（ESP32 → PC）を 1 つの Function に持てます。
- 音量・ミュート・rate の変更は `pollEvent()` で知らせるだけで、PCM には掛けません。I2S や codec はアプリ側で扱います。

### Video

- `EspUsbDeviceVideo` は UVC のカメラです。MJPEG か YUY2 の frame を `submitFrame()` で渡します。JPEG の encode はしません。
- 転送は Isochronous か Bulk を実行時に選べます。ESP32-P4 の HS では帯域が大きく取れます。

### NCM（USB ネットワーク）

- `EspUsbDeviceNet` は PC から USB ネットワークアダプタに見えます。最近の Windows・macOS・Linux は driver の導入なしで使えます。
- `beginNetwork()` で lwIP の netif を作り、`dhcpServer(true)` で PC にアドレスを配れます。Web の設定画面や API を USB 越しに見せる形です。
- IP を使わず、Ethernet frame を直接扱うこともできます（`onFrame()` / `sendFrame()`）。

### CCID（スマートカードリーダー）

- 1 slot、T=1、short APDU のリーダーです。カードの ATR と APDU への応答をスケッチで書きます（`insertCard()`、`onApdu()`）。
- PC の PC/SC アプリから、カードが挿さったリーダーとして見えます。

### DFU と firmware の更新

- `EspUsbDeviceDfu` の Download は、`dfu-util -D firmware.bin` で予備の OTA partition に書き、検証して再起動します。
  endpoint を使わないので、どの構成にも足せます。Runtime は `dfu-util -e` の DETACH だけに応えます。
- `EspUsbDeviceFirmwareUpdate` は経路に依存しない OTA の書込み器です。CDC、Vendor、NCM 越しの HTTP からも使えます。
- `device.rebootToBootloader()` は動作中のスケッチから ROM の download loader に入ります。
- chip ごとの boot mode と、各経路の比較は [USB 経由の OTA](docs/ota-over-usb.ja.md) にあります。

## 複合 device と endpoint

Function を必要なだけ宣言して `device.begin()` を一度呼ぶと、interface 番号と endpoint を割り当てて
複合 device の descriptor を作ります。上限は controller の資源です。

| controller | control 以外の IN endpoint | endpoint 番号 |
| --- | --- | --- |
| ESP32-S2 / S3 | 4 | 1〜6 |
| ESP32-P4 の FS controller | 4 | 1〜6 |
| ESP32-P4 の HS controller | 7 | 1〜15。bulk 512 byte、interrupt 1024 byte まで |

- IN endpoint の消費：CDC・NCM は 2 本、CCID は 2 本（カード固定なら 1 本）、Vendor・MIDI・MSC・Video は 1 本、
  HID は全 HID で 1 本を共有、DFU は 0 本。Audio は capture が 1 本、UAC2 の playback が feedback で 1 本です。
  たとえば S3 では、HID ＋ CDC ＋ MSC（1 ＋ 2 ＋ 1）で 4 本を使い切ります。
- `begin()` は endpoint の数と TX FIFO の容量を USB を動かす前に検査し、収まらなければ `ESP_ERR_INVALID_SIZE` を返します。
  組み立てた descriptor と予算は [Info](examples/Info/) の DescriptorDump で、Host なしで確かめられます。
- 例：[CompositeHidCdcMsc](examples/CompositeHidCdcMsc/)（keyboard ＋ serial ＋ FAT disk）。

## ESP32-P4 と HS

- P4 には HS（480 Mbps、UTMI PHY）と FS の 2 つの controller があります。`config.controller` の `Auto` は HS を選びます。
- HS では bulk packet が 512 byte になります。MSC の `transferBytes` は 512 の倍数、`readDirect()` の容量も 512 の倍数が必要です。
  FS で動いた設定でも、HS では `begin()` が拒否することがあります。
- `device.speed()` で Host と決まった速度が分かります。HS controller でも、FS の Host や hub につなぐと FS です。
- HS では USB 2.0 の TEST_MODE に応答します。
- 例：[P4HighSpeedDevice](examples/P4HighSpeedDevice/)、[P4FullSpeedDevice](examples/P4FullSpeedDevice/)。

## メモリと PSRAM

- buffer は `begin()` で heap から確保し、`end()` で解放します。失敗したときは何も残しません。
- CPU が読み書きする ring や MSC の保存先は、スケッチが渡せます。PSRAM も使えます。
- USB の DMA が触る領域（staging、借用転送の buffer）は内部 RAM です。借用転送に PSRAM の buffer を渡すと `NotDmaCapable` で拒否します。
- 詳しくは[詳細ガイド](docs/usb-device-advanced.ja.md)の DMA と PSRAM の章を見てください。

## 制限事項

- Arduino-ESP32 標準の `USB.begin()`、`USBHIDKeyboard`、`USBCDC` などとは同時に使えません。
- NCM は CDC-NCM だけです（CDC-ECM、RNDIS はありません）。ESP32 が PC 経由でインターネットへ出るには、PC 側の bridge や共有が要ります。
- CCID は 1 slot、T=1、short APDU です。extended APDU、chaining、PIN pad、T=0 は扱いません。
- Video は 1 つの Function に 1 つの形式・解像度・fps です。still image とカメラの制御は宣言しません。
- MSC は 1 Function で LUN 0 だけです。SD card を公開している間は、ESP32 側で同じ card の file API を使わないでください。
- Audio は音量やミュートを PCM に掛けません。外部 codec の clock のずれには追従しません（ring の水位を見てアプリ側で吸収します）。
- ISR からの呼出しには対応していません。

## 文書

| 文書 | 内容 |
| --- | --- |
| [USB device ガイド](docs/usb-device-guide.ja.md) | USB の基礎、電源と connector、ESP32 固有の注意、立ち上げ |
| [class の文書](docs/classes/) | class ごとの使い方、全設定、動作、Host OS 側の注意 |
| [詳細ガイド](docs/usb-device-advanced.ja.md) | task モデル、descriptor の詳細、control 転送、FIFO、DMA と PSRAM、自作の Function |
| [サンプル](examples/README.ja.md) | 40 本以上の完結したスケッチ。2.x の例もすべて含む |
| [トラブルシューティング](docs/troubleshooting.ja.md) | `begin()` のエラー、列挙の問題、OS ごと・class ごとの問題 |
| [USB 経由の OTA](docs/ota-over-usb.ja.md) | ファームウェア更新の経路：ROM loader、DFU、drive、serial、vendor、browser |
| [v2 からの移行](docs/migrating-from-v2.ja.md) | EspUsbDevice 2.x のスケッチを 3.0 へ |
| [Arduino-ESP32 USB からの移行](docs/migrating-from-arduino-esp32-usb.ja.md) | `USBHIDKeyboard`、`USBCDC`、`USBMSC` などから |
| [テスト](tests/README.ja.md) | 実機試験の構成と、確かめた範囲（[TEST_PLAN](tests/TEST_PLAN.ja.md)） |
| [変更履歴](CHANGELOG.md) | 版ごとの変更 |

## まだ確かめていないこと

Windows と macOS の Host、実際の SD card と OTA の書込み、ESP32-S2 の ROM DFU への移行、Arduino-ESP32 4.x。
試験の Host は ESP32 と Linux PC です。[TEST_PLAN](tests/TEST_PLAN.ja.md) を参照してください。

## ライセンス

MIT です。[LICENSE](LICENSE) を参照してください。DWC2 controller の driver は TinyUSB（MIT）と
ST のコード（BSD-3-Clause）に由来します。[third_party/tinyusb](third_party/tinyusb/PROVENANCE.ja.md) を参照してください。
