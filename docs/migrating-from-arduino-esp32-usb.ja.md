# Arduino-ESP32標準USB APIからの移行ガイド

> English: [migrating-from-arduino-esp32-usb.md](migrating-from-arduino-esp32-usb.md)

Arduino-ESP32 core標準のUSB device API（`USB.begin()`、`USBHIDKeyboard`、
`USBHIDMouse`、`USBCDC`、`USBMSC`、`USBMIDI`、`USBVendor`など）で書かれた
スケッチをEspUsbDeviceへ移すためのガイドです。EspUsbDeviceは**意図的に**この
スタックとAPI互換ではありません。port、speed、descriptor、endpoint packet
size、raw class reportをスケッチ側から明示的に制御するためです。このページでは
core APIごとの対応先、移行して得られるもの、どちらにも対応物がないものを
まとめます。

## モデルの違い（最初に読んでください）

| | Arduino-ESP32 core | EspUsbDevice |
|---|---|---|
| 起動 | クラスごとの`begin()`のあとにグローバルな`USB.begin()` | `EspUsbDevice`オブジェクト1つ。各クラスはconstructorで自己登録し、`device.begin(config)`1回 |
| VID / PID / 文字列 | `USB.VID()` / `USB.PID()` / `USB.productName()`などのsetter（またはビルドフラグ） | `EspUsbDeviceConfig`のフィールドとして`begin()`へ渡す |
| ディスクリプタ | coreが所有 | ライブラリが所有し、バイト単位で確認できる（[DescriptorDump](../examples/Info/EspUsbDeviceDescriptorDump/)） |
| イベント | `USB.onEvent()`の集中ハブ | クラスごとのcallback（`onOutputReport`、`onLineState`など）と`device.ready()` |
| エラー | ほぼ無言 | `begin()`が`false`を返し、`device.lastErrorName()`が理由を名乗る |
| 複合デバイス | クラスが暗黙に積み重なる | 最大4クラスを明示登録。endpoint予算はPHY開始前に検査される |

2つのスタックは**排他**です。EspUsbDeviceのスケッチでは`USB.begin()`を
呼ばず、`USB Mode`は"USB-OTG (TinyUSB)"でビルドし、`USB CDC On Boot`は無効の
ままにします。このため`Serial`はUART側に出ます
（[ガイド3.5](usb-device-guide.ja.md#35-arduino-esp32標準usbスタックとは排他)）。

### 移行チェックリスト

1. `#include "USB.h"`とすべての`USB.*`呼び出しを削除する。
2. `EspUsbDevice device;`を1つ作り、各クラスのconstructorへ渡す。
3. VID/PID/manufacturer/product/serialを`EspUsbDeviceConfig`へ移す。
4. クラスごとの`begin()`+`USB.begin()`のペアを、全クラス構築後の
   `device.begin(config)`1回に置き換える。
5. 送信は`device.ready()`でゲートする。未マウント中の送信は捨てられ、
   キューされません。
6. `begin()`の戻り値を確認し、`device.lastErrorName()`を出力する。

## Keyboard: `USBHIDKeyboard` → `EspUsbDeviceHidKeyboard`

移行前:

```cpp
#include "USB.h"
#include "USBHIDKeyboard.h"
USBHIDKeyboard Keyboard;

void setup() {
  Keyboard.begin();
  USB.begin();
}
void loop() {
  Keyboard.println("Hello");
  Keyboard.press(KEY_LEFT_GUI);
  Keyboard.press('r');
  Keyboard.releaseAll();
}
```

移行後:

```cpp
#include "EspUsbDevice.h"
EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);

void setup() {
  keyboard.setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_JA_JP);
  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4001;
  config.product = "My Keyboard";
  device.begin(config);
}
void loop() {
  if (!device.ready()) { delay(10); return; }
  keyboard.write("Hello\n");
  keyboard.tapUsage(ESP_USB_HID_KEY_R, ESP_USB_DEVICE_MOD_LEFT_GUI);
}
```

| core | EspUsbDevice | 補足 |
|---|---|---|
| `Keyboard.print()` / `println()` | `keyboard.write("text")` | レイアウト対応。`setLayout()`には**ホスト側**のレイアウトを指定 |
| `Keyboard.press('a')` / `release()` | `keyboard.pressKey('a')` / `releaseAll()` | ASCII経路 |
| `Keyboard.press(KEY_LEFT_GUI)`+キー | `keyboard.pressUsage(usage, modifiers)` / `tapUsage()` | 変換済みキーコードではなく、HID usage IDとmodifier bitが第一級API |
| `Keyboard.releaseAll()` | `keyboard.releaseAll()` | 同じ発想 |
| （対応なし） | `keyboard.sendReport(...)` | boot / NKROレポートをバイト単位で送信 |
| （対応なし） | `keyboard.enableNkro()` + `EspUsbDeviceNkroKeyboardReport` | 224bitビットマップによる真のN-key rollover |
| （対応なし） | `keyboard.ledState()` / `onOutputReport()` | ホストからのNumLock/CapsLock/ScrollLock |
| （対応なし） | `keyboard.onProtocol()` | BIOS/UEFIのboot protocol切り替えを検出 |

## Mouse: `USBHIDMouse` → `EspUsbDeviceHidMouse`

| core | EspUsbDevice | 補足 |
|---|---|---|
| `Mouse.move(x, y, wheel)` | `mouse.move(x, y, wheel)` | 同形 |
| `Mouse.click(MOUSE_LEFT)` | `mouse.click(ESP_USB_DEVICE_MOUSE_LEFT)` | 定数は`LEFT` / `RIGHT` / `MIDDLE` / `BACK` / `FORWARD` |
| `Mouse.press()` / `release()` | `mouse.press(buttons)` / `release(buttons)` / `releaseAll()` | ボタンマスク |
| （対応なし） | `mouse.sendReport(...)` | raw boot mouseレポート |

`USBHIDAbsoluteMouse`に**対応物はありません**。このライブラリは相対移動の
boot mouseのみ実装しています。

## メディアキーとその他HID: consumer / system control / gamepad / vendor HID

- `USBHIDConsumerControl` → [`EspUsbDeviceHidConsumerControl`](../examples/MediaKeys/)。
  `media.click(ESP_USB_DEVICE_CONSUMER_CONTROL_VOLUME_UP)`など。任意の16bit
  consumer usageも`press()` / `sendUsage()`で送れます。
- `USBHIDSystemControl` → `EspUsbDeviceHidSystemControl`（power / sleep / wake）。
- `USBHIDGamepad` → [`EspUsbDeviceHidGamepad`](../examples/Gamepad/)。
  `gamepad.send(x, y, z, rz, rx, ry, hat, buttons)`の1呼び出し。
- `USBHIDVendor` → [`EspUsbDeviceHidVendor`](../examples/VendorHID/)。
  interrupt IN/OUTとfeature reportを持つvendor定義HID。
- `USBHID`+`USBHIDDevice`のサブクラス化 →
  [`EspUsbDeviceHidCustom`](../examples/CustomHID/)。report descriptorの
  バイト列をそのまま渡します。サブクラス化は不要です。

上記のHIDクラスは複合時に**1つのHIDインターフェース**へreport IDで統合される
ため、keyboard + mouse + メディアキーの構成でもIN endpointは1本です
（[ガイド3.3](usb-device-guide.ja.md#33-endpoint予算)）。

## Serial: `USBCDC` → `EspUsbDeviceCdcSerial`

移行前:

```cpp
#include "USB.h"
USBCDC USBSerial;

void setup() {
  USBSerial.begin();
  USB.begin();
}
void loop() { USBSerial.println("tick"); }
```

移行後:

```cpp
#include "EspUsbDevice.h"
EspUsbDevice device;
EspUsbDeviceCdcSerial UsbSerial(device);

void setup() {
  UsbSerial.onLineState([](const EspUsbDeviceCdcLineState &s) {
    // s.dtr / s.rts
  });
  EspUsbDeviceConfig config;
  device.begin(config);
}
void loop() {
  if (UsbSerial.connected()) UsbSerial.write((const uint8_t *)"tick\n", 5);
}
```

`read()` / `available()` / `write()` / `flush()`はそのまま移せます。coreの
`onEvent(ARDUINO_USB_CDC_*)`イベントは、型付きcallbackの`onLineCoding()`、
`onLineState()`、`onRx()`になります。このCDCはスケッチ用のデータポートで
あり、Arduinoコンソールではありません（`Serial`はUART側に出ます。
[ガイド2.3](usb-device-guide.ja.md#23-開発中のコネクタ構成)）。

## Mass storage: `USBMSC` → `EspUsbDeviceMsc`

callback3点セットはほぼそのまま移り、容量指定が`begin()`から外れます。

| core | EspUsbDevice |
|---|---|
| `msc.vendorID("ESP32")` / `productID()` / `productRevision()` | 同名 |
| `msc.onRead()` / `onWrite()` / `onStartStop()` | 同名 |
| `msc.mediaPresent(true)` | 同名。加えて`isWritable(bool)` |
| `msc.begin(blockCount, blockSize)` | 容量はattachしたバックエンドが持つ。登録はconstructorで行われる |

新しいのは既製バックエンドで、ほとんどのスケッチはblock callbackを書かずに
済みます。[`EspUsbDeviceMscRamDisk`](../examples/MSC/)（生ブロック）、
[`EspUsbDeviceMscFatRamDisk`](../examples/MSCFatRamDisk/)（ファイルヘルパー
付きの本物のFAT12イメージ）、[`EspUsbDeviceMscSdCard`](../examples/MSCSdCard/)
（SDカードパススルー）。いずれも`disk.attach(msc)`で接続します。

## MIDI: `USBMIDI` → `EspUsbDeviceMidi`

```cpp
EspUsbDeviceMidi MIDI(device);          // または (device, inCables, outCables)

MIDI.noteOn(0, 60, 96);                 // channel, note, velocity[, cable]
MIDI.noteOff(0, 60, 0);
MIDI.controlChange(0, 74, 32);

EspUsbDeviceMidiPacket packet;
while (MIDI.readPacket(packet)) { /* 4バイトのUSB-MIDIイベント */ }
```

引数順に注意してください。**ここではchannelが先頭**（0始まり）で、任意の
USB-MIDI **cable**番号が末尾です。coreのヘルパーはchannelを末尾に取るため、
機械的に移植するとコンパイルは通るのに違う音が出ます。複数cable（in/out
非対称も可）と生の4バイトpacket I/Oはcoreに対応物がありません。
[`MIDIInterface`](../examples/MIDIInterface/)を参照してください。

## Vendor: `USBVendor` → `EspUsbDeviceVendor`

| core | EspUsbDevice |
|---|---|
| `Vendor.write()` / `read()` / `available()` | 同名（bulk IN/OUT） |
| `Vendor.onEvent()`でのリクエスト処理 | `onControlRequest()` + `sendControlResponse()` |
| `USB.webUSB(true)` / `USB.webUSBURL(url)` | `config.webusbEnabled = true` / `config.webusbUrl = "..."` |

`webusbEnabled`を有効にするとBOSとMS OS 2.0 descriptorを返すので、Windowsは
WinUSBを自動でバインドします。ZadigもINFも不要です。ブラウザ側との接続は
[`USBVendor`](../examples/USBVendor/)を参照してください。

## EspUsbDeviceにしかないもの

- **USB Audio**（UAC1がdefault、UAC2は明示選択）: speaker、microphone、
  duplex headsetの各stream。coreにはaudio classがそもそもありません。
- **CDC-NCMネットワークデバイス**（`EspUsbDeviceNet`）: ボードがUSBネットワーク
  インターフェースになり、背後はlwIP/esp_netifです。
- **CCIDスマートカードリーダー**（`EspUsbDeviceCcid`）。
- **NKRO keyboard**、raw report API、boot protocolの観測。
- **ESP32-P4のcontroller選択**（FS / HS）: `config.controller`。
- ホストを繋ぐ前にできるバイト単位のディスクリプタ確認とendpoint予算の表示。

## まだ無いもの

- `USBHIDAbsoluteMouse` - 相対移動のboot mouseのみです。
- `FirmwareMSC`（MSC経由のファーム更新）- 予定あり。[TODO.ja.md](../TODO.ja.md)
  で追跡しています。
- USBコンソール（`USB CDC On Boot` / `HWCDC`）- 設計上の判断です。ログは
  UART側に出します。

移植後に動きがおかしいときは[troubleshooting.ja.md](troubleshooting.ja.md)を
上から辿ってください。特に
[ディスクリプタキャッシュの項](troubleshooting.ja.md#変更したのにホストで古いデバイスのまま見える)
は要注意です。core時代のVID/PIDのまま書き換えたボードは、移行直後の数分で
Windowsを混乱させます。
