# EspUsbDevice

EspUsbDevice は、新しい ESP32 Arduino USB Device ライブラリです。

Arduino-ESP32 標準の `USB`、`USBHIDKeyboard`、`USBHIDMouse` API との互換は
目標にしません。port、speed、descriptor、endpoint packet size、raw class
report をスケッチから明示的に制御できる、よりよい小さな USB Device ライブラリを
目指します。

最初の実装対象は `EspUsbHost` の peer / loopback テストです。これは実ハードウェアで
具体的に検証でき、ライブラリが制御すべき低レベル挙動を明確にできるためです。
テスト向けの機能を先に実装しますが、それはプロジェクトの最終的な範囲ではありません。

## 対応環境

対応する Arduino-ESP32 コア（ボードパッケージ）の最低バージョン:

| ターゲット | 最低 arduino-esp32 |
| --- | --- |
| ESP32-S2 / ESP32-S3 / ESP32-P4 | 3.3.9 |

これより古いコアは非対応です（3.3.8 以前はビルドに失敗します）。ライブラリ各バージョンのコア別ビルド結果は [`docs/`](docs/) に `COMPATIBILITY.<version>.md` として公開しています。

## リリース範囲

このリリースでは、HID keyboard / mouse / gamepad / consumer / system / custom / vendor HID、
CDC ACM、USB MIDI、MSC、USBVendor、USB Audio（speaker / microphone）、CDC-NCM
ネットワークデバイス、多機能な複合デバイスを扱えます。

代表的な用途:

- layout 対応 keyboard、raw HID usage、mouse / gamepad / media key を送る。
- PC や `EspUsbHost` と CDC ACM serial / USB MIDI で通信する。
- RAM disk、FAT RAM disk、SD card を USB MSC として公開する。
- HID ではない vendor-specific bulk/control interface を作る。
- USB Audio の speaker / microphone PCM を callback で送受信する。
- ボードを USB ネットワークアダプタ（CDC-NCM）として見せ、任意で lwIP/DHCP を有効にして
  PC が USB 経由でデバイス上のページや API にアクセスできるようにする。
- 上記を組み合わせて 1 つの複合デバイスにする。

## 設計目標

- `EspUsbHost` と同じように、明示設定と callback ベースの API にする。
- Arduino USB class descriptor に依存せず、descriptor はこのライブラリで所有する。
- HID は文字入力ではなく usage ID と raw report を第一級 API にする。
- ESP32-S3 2台構成の peer テストと、ESP32-P4 1台構成の loopback テストを
  初期検証ターゲットとして支える。
- Arduino-ESP32 標準 USB Device stack とは排他利用にする。このライブラリを使う
  スケッチでは `USB.begin()` を呼ばない。

## 現在のスコープ

最初のマイルストーンは、既存 `EspUsbHost` peer device を置き換え、実ハードウェアで
コア API を検証することです。HID MVP から始め、CDC ACM、USB MIDI、MSC まで peer /
loopback テストで確認できる範囲を広げています。

- device port / speed / VID / PID / string / power 設定。
- speed に応じた descriptor 生成と endpoint MPS 選択。
- HID boot keyboard の raw report 送信。
- HID keyboard output report callback による LED 状態受信。
- HID boot mouse の raw report 送信。
- HID consumer / system / gamepad / custom / vendor report。
- CDC ACM serial。
- USB MIDI event packet と note / control change helper。
- USB MSC block device と SCSI callback。
- USBVendor bulk IN/OUT、control request、WebUSB landing URL。
- USB Audio speaker / microphone PCM callback。
- CDC-NCM ネットワークデバイス（生フレーム API と、任意の lwIP/esp_netif 統合＝DHCP
  サーバ / クライアント / 静的アドレス）。
- 多機能な複合デバイス（例: HID + CDC + MSC を 1 台に）。
- pytest-embedded peer / loopback テスト用の serial command sketch。

USB Audio の PCM はこのライブラリの責務（USB Audio class と PCM callback）までで、受け取った
PCM はアプリケーション、PCMFlow、PCMFlowDevice など任意の処理系へ渡します。

- PCMFlow: https://github.com/tanakamasayuki/PCMFlow
- PCMFlowDevice: https://github.com/tanakamasayuki/PCMFlowDevice

## 最小例

Keyboard:

```cpp
#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);

void setup()
{
  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4001;
  config.product = "EspUsbDevice Keyboard";
  device.begin(config);
}

void loop()
{
  if (device.ready())
  {
    keyboard.write("hello");
    delay(1000);
  }
}
```

CDC ACM serial:

```cpp
#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceCdcSerial SerialUSB(device);

void setup()
{
  EspUsbDeviceConfig config;
  config.product = "EspUsbDevice Serial";
  device.begin(config);
}

void loop()
{
  if (SerialUSB.connected())
  {
    SerialUSB.println("hello");
    delay(1000);
  }
}
```

## Examples

ユーザー向けの基本 sketch は [examples/README.ja.md](examples/README.ja.md) にまとめています。

- `Keyboard`: layout 付き ASCII 文字列と HID usage ID を送信する boot keyboard。
- `KeyboardNKRO`: 任意数のキーを同時押下できる N-key rollover keyboard。
- `Mouse`: 移動、wheel、button を送信する boot mouse。
- `KeyboardMouse`: keyboard + mouse の composite HID。
- `Gamepad`: axes、hat、button を送信する HID gamepad。
- `MediaKeys`: volume、再生停止、system control を送信する HID media keys。
- `VendorHID`: 独自 63 byte report を送受信する vendor-defined HID。
- `USBVendor`: bulk IN/OUT と control request を扱う vendor-specific interface。
- `CustomHID`: sketch 定義の HID report descriptor を使う custom HID。
- `Serial`: PC / Host とテキストを送受信する CDC ACM serial。
- `MIDI`: note / control change を送受信する USB MIDI device。
- `MIDIController`: ADC / button input を MIDI CC / note に変換する controller。
- `MIDIInterface`: UART MIDI 1.0 と USB MIDI 1.0 の bridge。
- `MSC`: RAM buffer を block device として公開する Mass Storage Class。
- `MSCFatRamDisk`: RAM 上の FAT12 disk で Host とファイルを受け渡す Mass Storage Class。
- `MSCSdCard`: SPI SD card を Host へ USB storage として公開する Mass Storage Class。
- `UsbNetwork`: DHCP サーバと `http://192.168.7.1/` の Web ページを持つ CDC-NCM
  ネットワークデバイス（USB 経由でアクセス）。
- `CompositeHidCdcMsc`: HID keyboard + CDC serial + MSC FAT RAM disk を 1 台にした複合デバイス。

## HID Keyboard / Mouse APIs

Keyboard:

- `keyboard.setLayout(layout)` は EspUsbHost と同じ layout ID と keymap table を使い、
  Device 側では ASCII から usage への逆変換に使います。
- `keyboard.write(text)`、`tapKey(key)`、`pressKey(key)` は文字向けの上位 helper です。
- `keyboard.tapUsage()`、`pressUsage()`、`releaseUsage()`、`releaseAll()`、
  `sendReport()` で raw HID usage / report 制御もできます。
- `keyboard.onOutputReport(callback)` は Host からの LED output report を受け取ります。
- `keyboard.enableNkro()`（`begin()` の前）で N-key rollover に切り替えます。usages
  `0x00`-`0xDF` をカバーする bitmap レポート（International/LANG キーも含むので JIS
  レイアウトも通る）で任意数のキーを同時押下でき、BIOS 向けに6キー boot へ自動 fallback
  します。既定は無効です。

Mouse:

- `mouse.move(x, y)`、`wheel(delta)`、`sendReport(report)` は移動と raw report を送信します。
- `mouse.press(buttons)`、`release(buttons)`、`releaseAll()`、`click(button)`、
  `buttons()` は Device 側 button 状態を保持して扱います。

## CDC / MIDI / MSC APIs

CDC ACM:

- `EspUsbDeviceCdcSerial` は USB serial の read / write callback と helper を提供します。
- `available()`、`read()`、`write()`、`print()` 系の Arduino らしい使い方と、
  raw callback の両方を扱えます。

USB MIDI:

- `EspUsbDeviceMidi` は 4 byte USB-MIDI event packet を送信します。
- `noteOn()`、`noteOff()`、`controlChange()` などの helper と `writePacket()` を併用できます。

MSC:

- `EspUsbDeviceMsc` は inquiry、media 状態、capacity、read/write callback を扱います。
- `EspUsbDeviceMscRamDisk` は外部 RAM buffer を block device として公開する helper です。
- `EspUsbDeviceMscFatRamDisk` は RAM 上に小さい FAT12 image を作り、Host との一時ファイル
  受け渡しに使う helper です。
- `EspUsbDeviceMscSdCard` は Arduino `SD` の raw sector I/O を MSC に接続する helper です。
- MSC は block device と filesystem が別です。OS からドライブとしてマウントするには、
  有効な FAT image か SD card などの実 storage を read/write callback に接続してください。
- flash / SPIFFS / LittleFS の直接公開は標準方針にしません。永続ストレージは SD card、
  一時ファイル受け渡しは RAM disk + FAT helper を優先します。

## Network / Composite APIs

USB ネットワーク（CDC-NCM）:

- `EspUsbDeviceNet` はボードを USB ネットワークアダプタとして見せます。最近の Windows /
  macOS / Linux は標準の NCM ドライバをインストール不要で bind します。
- `onFrame()` / `sendFrame()` は生 Ethernet フレームを扱います。`beginNetwork()` を
  呼ばなければ生フレーム transport のままです（PC 側ブリッジ実験に有用）。
- `beginNetwork()` は lwIP/esp_netif インターフェースを起動します。アドレス方式は
  `dhcpServer(true)`（デバイスが gateway、host に配布）、`dhcpClient(true)`（PC ブリッジ
  LAN から取得）、`ipConfig(...)`（静的）から選択。DHCP は opt-in です。サブネットは既定で
  `192.168.7.0/24`（デバイスは `192.168.7.1`）ですが、`beginNetwork()` の前に
  `ipConfig(local, gateway, subnet)` を渡せば変更でき、DHCP サーバの配布レンジも設定した
  IP/mask に自動追従します。
- DHCP サーバは既定で gateway/DNS を広告しません（host の実インターネット経路をブラックホール
  化しないため）。実際に転送する/到達可能な DNS がある場合は `dhcpAdvertiseGateway(true)` /
  `dhcpDns(ip)` で opt-in します。
- USB netif は route priority を低くしてあり、Wi-Fi STA 併用時は Wi-Fi が ESP のデフォルト経路の
  ままです。`defaultRoute(true)` で USB ホストを ESP の uplink にできます（PC がブリッジ/NAT する
  構成 + `dhcpClient(true)`）。
- ホストに見せる MAC は、既定でこのチップ固有の Ethernet MAC（`esp_read_mac` / `ESP_MAC_ETH`）を
  使います。個体ごとに一意で、Wi-Fi STA/AP・BT の MAC とも重複しないため、NCM と Wi-Fi を同時に
  使っても自分自身と衝突しません。1 台の PC に 1 台なら常に問題なく、同一の 2 枚のボードを同じ PC に
  挿しても MAC が異なるので動作します。`begin()` の前に `macAddress(mac)` を呼べば任意の MAC に
  固定できます（ただし 2 枚を同じ MAC に固定すると同一ホスト上で衝突し、`dhcpServer(true)` の
  2 台は既定で `192.168.7.0/24` サブネットが重複します。各デバイスに別々の `ipConfig(...)`
  サブネットを与えれば、1 台のホストに複数台を共存させられます）。

複合:

- 複数クラスを 1 つの `EspUsbDevice` に登録し `begin()` を 1 回呼ぶだけで、interface 番号と
  endpoint が割り当てられ複合 descriptor が構成されます。`CompositeHidCdcMsc` を参照。

## 制限事項

- Arduino-ESP32 標準の `USB.begin()`、`USBHIDKeyboard`、`USBHIDMouse` などとは併用しません。
- USB Audio（`EspUsbDeviceAudio`）は単独 device の speaker / microphone 実装で、排他です（他 class と複合できません）。I2S、codec、DAC などのデバイス接続はこのライブラリの責務外です。
- ネットワークデバイスは CDC-NCM のみです。CDC-ECM は Arduino-ESP32 core で無効（有効化には core 再ビルドが必要）で、NCM は最近のホスト OS が標準対応します。デバイスが PC 経由でインターネットに抜けるにはホスト側のブリッジ/NAT が必要でスコープ外です（その用途は ESP 自身の Wi-Fi を使用）。
- 複合デバイスは ESP32-S3 の USB endpoint 予算（FIFO 消費 IN が約 3 本）で制限されます。4 本目は ESP32-P4 が必要です。USB Audio は複合に含められません。
- MSC は block device と filesystem を分けて扱います。Host から通常の drive として mount
  するには FAT RAM disk helper または SD card などを使います。
- flash / SPIFFS / LittleFS を USB MSC として直接公開することは標準方針にしません。
- SD card を MSC として Host に公開している間は、ESP32 側で同じ card の file API を使わないでください。
- WebUSB / Microsoft OS 2.0 descriptor の基本応答は Arduino-ESP32 TinyUSB core に依存します。
  custom vendor code、GUID、Microsoft OS 2.0 descriptor 内容の差し替え API は未実装です。

テスト構造と段階的なカバレッジ計画は [tests/TEST_PLAN.ja.md](tests/TEST_PLAN.ja.md)
を参照してください。
設計背景と `EspUsbHost` 既存テストからの移行メモは [docs/DESIGN_NOTES.ja.md](docs/DESIGN_NOTES.ja.md)
にまとめています。
現在の開発方針と残作業は [docs/DEVELOPMENT_PLAN.ja.md](docs/DEVELOPMENT_PLAN.ja.md)
にまとめています。
リリース前確認は [docs/RELEASE_CHECKLIST.ja.md](docs/RELEASE_CHECKLIST.ja.md) を参照してください。
