# EspUsbDevice

> English: [README.md](README.md)

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

これより古いCoreは公式対応外です。ライブラリ各versionのCore別build結果は
[`docs/`](docs/)に`COMPATIBILITY.<version>.md`として公開し、現在のworktreeを
実行したdraftは固定名`COMPATIBILITY.WORKTREE.md`を使用します。
自動matrixは3.3.0以降を観測しますが、3.3.9未満の結果は参考情報であり、公式対応を意味しません。

## ライブラリ所有のTinyUSB stack

v2ではArduino-ESP32がprebuildしたTinyUSB configuration、initializer、task、endpoint
allocator、descriptor loaderを使用しません。EspUsbDevice自身の`tusb_config.h`で固定・
選択したTinyUSB sourceをbuildし、ESP-IDFのPHY/controllerを直接初期化してdevice taskを
実行し、device/configuration/string/BOS/class descriptorを返します。

Arduino-ESP32はESP-IDFのSoC、PHY、FreeRTOS、board supportを得るplatform dependencyとして
引き続き使用します。切ったのはArduino USB Device integrationへの依存であり、Core全体を
forkしたわけではありません。選択したfile、pin、license、更新方針、byte-for-byte検証は
[TinyUSBの由来と管理方法](third_party/tinyusb/PROVENANCE.ja.md)に記録しています。

この境界をライブラリで所有した結果、次が可能になりました。

- ESP32-P4のFullSpeed/HighSpeed controllerをruntimeに選ぶ。
- negotiated speedに合わせたFS/HS endpoint packet size、device qualifier、
  other-speed configurationを返す。
- compositeのinterface/endpointを一括採番し、controllerごとに不可能な構成をPHY開始前に拒否する。
- 実際に割り当てたvendor interface用のWebUSB / Microsoft OS 2.0 descriptorを生成する。
- enableするclassとTinyUSB bufferをArduino-ESP32 prebuiltの`CFG_TUD_*`値から独立して設定する。
- TinyUSB自身が実装していないclass（CCID）を、同梱sourceに手を入れずapplication
  class driver hookから追加する。
- `USB.begin()`や`esp32-hal-tinyusb`へfallbackせずruntimeを終了・再初期化する。

## リリース範囲

このリリースでは、HID keyboard / mouse / gamepad / consumer / system / custom / vendor HID、
CDC ACM、USB MIDI、MSC、USBVendor、USB Audio（speaker / microphone）、CDC-NCM
ネットワークデバイス、CCID スマートカードリーダー、多機能な複合デバイスを扱えます。

代表的な用途:

- layout 対応 keyboard、raw HID usage、mouse / gamepad / media key を送る。
- PC や `EspUsbHost` と CDC ACM serial / USB MIDI で通信する。
- RAM disk、FAT RAM disk、SD card を USB MSC として公開する。
- HID ではない vendor-specific bulk/control interface を作る。
- 実転送を検証済みのUAC1 Audio Playback/Capture PCMをbounded FIFO経由で読み書きする。
  UAC2は明示選択でき、EspUsbHost 2.7.1のUAC2 hostに対する2台テスト `peer/usb_audio_uac2` でend-to-endにカバー。
- ボードを USB ネットワークアダプタ（CDC-NCM）として見せ、任意で lwIP/DHCP を有効にして
  PC が USB 経由でデバイス上のページや API にアクセスできるようにする。
- ボードを USB スマートカードリーダー（CCID）として見せ、カードの中身をスケッチで実装して
  PC/SC ホストからの APDU に応答する。
- 上記を組み合わせて 1 つの複合デバイスにする。

## 設計目標

- `EspUsbHost` と方向・PCM format・control値の語彙を揃え、明示設定を基本にする。
  callbackの実行contextが適切でない高頻度I/Oはbounded polling APIにする。
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
- UAC1 defaultのAudio Playback/Capture polling I/O、チャンネル別mute/volume state、
  control event、stream stats。UAC2は明示選択でき、Clock Sourceによるrate制御、
  UAC2のFeature Unit layout、双方向streamingをpeer testで検証済みです。
- CDC-NCM ネットワークデバイス（生フレーム API と、任意の lwIP/esp_netif 統合＝DHCP
  サーバ / クライアント / 静的アドレス）。
- CCID スマートカードリーダー（1 slot、スケッチが与える ATR、APDU / escape callback、
  カード挿抜通知）。
- 多機能な複合デバイス（例: HID + CDC + MSC を 1 台に）。
- pytest-embedded peer / loopback テスト用の serial command sketch。

USB Audioの責務はAudio classとPCM FIFO境界までです。受け取ったPCMはアプリケーション、
PCMFlow、PCMFlowDeviceなど任意の処理系へ渡します。volume/muteをPCMへ暗黙適用しません。
`hasMute()` / `getMute()` / `setMute()`と対応するvolume APIは、USB Hostへ公開する
Master/Left/Rightのcontrol stateを共有します。volume値はUSB Audio wire形式と同じ
1/256 dB単位です。

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

診断用の sketch は `examples/Info/` にあります。動かないときはここから始めてください。
使い方と切り分けの手順は [docs/usb-device-guide.ja.md](docs/usb-device-guide.ja.md) にまとめています。

- `Info/EspUsbDeviceBringUpCheck`: 起動、列挙、速度、Host → Device 疎通を順に確認する。**最初に動かす。**
- `Info/EspUsbDeviceDescriptorDump`: 組み立てられた descriptor 全部と endpoint 予算を表示する（Host 接続不要）。
- `Info/EspUsbDeviceConsole`: Serial から手打ちで HID report / vendor 転送を送り、Host からの要求を表示する。

機能別:

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
- `SmartCardReader`: Get UID と echo 命令に応答する擬似カードを持つ CCID スマートカードリーダー。
- `CompositeHidCdcMsc`: HID keyboard + CDC serial + MSC FAT RAM disk を 1 台にした複合デバイス。

## HID Keyboard / Mouse APIs

Keyboard:

- `keyboard.setLayout(layout)` は EspUsbHost と同じ layout ID と keymap table を使い、
  Device 側では ASCII から usage への逆変換に使います。
- `keyboard.write(text)`、`tapKey(key)`、`pressKey(key)` は文字向けの上位 helper です。
- `keyboard.tapUsage()`、`pressUsage()`、`releaseUsage()`、`releaseAll()`、
  `sendReport()` で raw HID usage / report 制御もできます。
- `keyboard.onOutputReport(callback)` は Host からの LED output report を受け取ります。
- `keyboard.ledState()` は Host からの最新 LED 状態（`EspUsbDeviceHidKeyboardOutputReport`）を
  **値で**返します。callback の有無に関係なく更新されるので、統合レイヤが `onOutputReport()` の
  単一 slot を占有していてもスケッチ側から Lock 状態を読めます（外付け Caps Lock LED を
  光らせる、など）。LED は event ではなく状態なので polling で足り、callback は単一 slot の
  ままです。Host が最初の output report を送るまでは全て false で、bus reset / 抜線でも
  クリアされます。
  参照ではなく値を返すのは、この値を書くのが TinyUSB device task で、読むのはスケッチの
  task だからです（参照を返すと他 task が書き換える実体を読むことになる）。raw LED byte を
  atomic に保持し、1 回の読み出しから report を組み立てるので、フィールドが途中状態で
  混ざることはありません。EspBle の `ledState()` も同じ理由で値返しです。
- `keyboard.enableNkro()`（`begin()` の前）で N-key rollover に切り替えます。usages
  `0x00`-`0xDF` をカバーする bitmap レポート（International/LANG キーも含むので JIS
  レイアウトも通る）で任意数のキーを同時押下でき、BIOS 向けに6キー boot へ自動 fallback
  します。既定は無効です。
- `keyboard.sendReport(EspUsbDeviceNkroKeyboardReport)` は**保持キー全体を1レポートで**
  送ります。7キー以上の同時押下、あるいは毎周期に状態全体を書く用途はこちらです
  （`pressUsage()` / `releaseUsage()` の増分 API では1キーの変化ごとに1レポートになり、
  同時押し・同時離しが分割されます）。`enableNkro()` 前だと失敗します。
- `EspUsbDeviceNkroKeyboardReport` は `modifiers` と usage `0x00`-`MaxBitmapUsage`
  (`0xDF`) の28-byte `bitmap` を持ち、`clear()` / `press()` / `release()` / `isDown()`
  で操作します。modifier usage `0xE0`-`0xE7` は bitmap 範囲外なので `press()` / `release()`
  が `modifiers` へ振り分け、呼び出し側は usage の区別を意識しません。`press()` / `release()`
  が false を返すのは、このレポートで表現できない usage（`0xDF` 超で modifier でもない）
  のときだけです。
- **bitmap を持つメンバは `bitmap`、usage の配列を持つメンバは `keys`** という規則です。
  6KRO の `EspUsbDeviceBootKeyboardReport::keys[6]` は usage 配列で、`keys[0] = 0x04` が
  型によって別の意味になり取り違えてもコンパイルが通るため名前を分けています。姉妹
  ライブラリ `EspUsbHost` / `EspBle` も同じ規則です。bitmap のサイズは Host 側が
  usage `0x00`-`0xFF` の32 byte、Device 側が report descriptor の宣言範囲に合わせた
  `0x00`-`0xDF` の28 byte で**非対称**です。
- `keyboard.heldState()` は Host へ最後に伝えた NKRO 状態を返します。同一状態の再送抑制と、
  `releaseAll()` によらない再同期に使えます（ライブラリ側では再送を抑制しません）。
  Boot protocol 選択中は「要求した状態」であって電波上のバイト列ではありません
  （6キーへ畳まれるため）。

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
- `EspUsbDeviceMidi(device, cableCount)` で最大 16 本の cable を公開できます。Host からは
  cable ごとに別々の MIDI port として見えます。既定は 1 本です。cable は 1 組の bulk endpoint
  を共有し、各 helper の 0 始まりの `cable` 引数、または `EspUsbDeviceMidiPacket::header` の
  上位 nibble で指定します。
- `EspUsbDeviceMidi(device, inCableCount, outCableCount)` で方向ごとに異なる cable 数を
  指定できます（実際の MIDI インターフェースでは非対称が普通です）。名前はどちらも Host から
  見た方向で、USB の endpoint 方向や EspUsbHost の `EspUsbHostMidiPortInfo` と同じです——
  IN が device → Host（device が送る側）、OUT が Host → device です。`noteOn()` などの送信
  helper の上限は `inCableCount()` なので、受信専用の cable への送信は、他の port に載ることなく
  失敗します。
- cable ごとの名前付けは未実装です（port 名は Host 側が付けます）。
- ESP-IDF の USB Host（EspUsbHost）と組み合わせる場合、cable 数の上限は **5 本**です。
  この Host は enumeration の control transfer より長い configuration descriptor を拒否し、
  `CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE` は Arduino のプリコンパイル済みライブラリで
  256 固定、スケッチからは変更できません。5 cable は configuration header を含めて 229 byte で
  enumerate でき、6 cable は 261 byte で `CHECK_SHORT_CONFIG_DESC FAILED` になります
  （`tests/peer/usb_midi_cables` で実測）。16 本は USB 仕様上は正当で PC の Host は受け付けます。
  制約は Host スタック側にあり、本ライブラリ側ではありません。

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

## USB Audio APIs

旧card型の`EspUsbDeviceAudio`実装は削除しました。新Audioは1つのfunctionへ、
Playback（Host→Device）とCapture（Device→Host）のstreamを独立して追加します。

```cpp
EspUsbDevice device;
EspUsbAudioFunction audio(device); // defaultはUAC1

auto &playback = audio.addPlaybackStream();
playback.addFormat({48000, 2, 2, 16});

auto &capture = audio.addCaptureStream();
capture.addFormat({48000, 1, 2, 16});
```

- `EspUsbAudioPlaybackStream::available()` / `read()`でspeaker PCMを消費する。
- `EspUsbAudioCaptureStream::write()`でmicrophone PCMを供給する。
- format fieldは`sampleRate`、`channels`、`bytesPerSample`、`bitsPerSample`で、
  EspUsbHostと語彙を揃える。
- `pollEvent()`はbounded queueからstream state、sample rate、mute、volume変更を返す。
  高頻度のアプリ処理、I2S write、user callbackでTinyUSB taskをblockしないためPCMは
  polling APIにしている。
- `stats()`、`resetStats()`、`clearBuffer()`でtransfer、overrun、underrunを観測でき、
  Audio Card固有のreceive task内部へ隠さない。
- Master/Left/RightのstateはUSB Feature Unit requestとmute/volume APIで共有する。
  volumeはsigned 1/256 dB wire単位。
- mute、volume、downmixなどのDSPを暗黙適用しない。I2S、codec、microphone、speaker、
  DSPはapplicationまたは任意のPCMFlow/PCMFlowDevice側の責務とする。

UAC1はdefaultで、S3のspeaker、microphone、duplex Peer streaming、control変更、
16/24/32-bit descriptor/transferを検証済みです。UAC2は
`EspUsbAudioFunction(device, EspUsbAudioProtocol::Uac2)`で選択します。descriptor、
Clock Sourceによるsample rate制御、Feature Unitのmute/volume（masterとlogical
channel）、非同期playback interfaceのexplicit feedback endpointを含む双方向
streamingを、EspUsbHostのUAC2 hostに対する2台テスト `peer/usb_audio_uac2` で
検証済みです。UAC2 functionが宣言するsample rateは方向ごとに1つです（descriptor
builderがalternate settingを1つだけ出力するため、Clock Sourceが報告するrateも1つ）。

新Audio sourceはUSB Audio仕様とTinyUSB公開driver APIから独立設計しました。旧
Espressif USBAudioCard由来sourceを継続改変せず削除しています。詳細は
[Audio source provenance](docs/V2_AUDIO_PROVENANCE.ja.md)を参照してください。

## CCID（スマートカードリーダー）APIs

- `EspUsbDeviceCcid` はボードを 1 slot の USB CCID リーダーとして見せます
  （`bInterfaceClass` 0x0b、bulk IN/OUT と挿抜通知用の interrupt IN）。
  slot の中のカードはスケッチが実装します。
- `insertCard(atr, length)` / `removeCard()` がカードの有無と `IccPowerOn` で返す ATR を
  決め、いずれも interrupt endpoint で Host へ通知します。状態は `cardPresent()` /
  `cardPowered()` で読めます。
- `onApdu(callback)` が各 exchange に応答します。callback は APDU を受け取り SW1SW2 を
  含む応答を書くので、カードの正体を決めるのはスケッチです。callback 未設定時は、命令を
  知らないカードと同じ 6D00 を返します。`onEscape(callback)` は vendor 固有の
  `PC_to_RDR_Escape` 用、`onPower(callback)` は活性化の通知です。
- slot status、活性化、parameter、abort の各メッセージはライブラリ側で応答するので、
  仕様に従う Host に対してスケッチ側の対応は不要です。
- callback は TinyUSB device task で実行されます。長く止めず、中から USB API を
  呼び返さないでください。

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
- USB Audioは`EspUsbAudioFunction`によるPlayback/Capture実装です。互換性重視の
  UAC1がdefaultで、UAC2は
  `EspUsbAudioFunction(device, EspUsbAudioProtocol::Uac2)`で明示選択します。
  UAC2 functionは方向ごとにsample rate 1つ・alternate setting 1つを宣言するため、
  Hostからrateを切り替えることはできません。I2S、codec、DACなどのデバイス接続は
  このライブラリの責務外です。
- ネットワークデバイスは CDC-NCM のみです。CDC-ECM は Arduino-ESP32 core で無効（有効化には core 再ビルドが必要）で、NCM は最近のホスト OS が標準対応します。デバイスが PC 経由でインターネットに抜けるにはホスト側のブリッジ/NAT が必要でスコープ外です（その用途は ESP 自身の Wi-Fi を使用）。
- CCID リーダーは 1 slot、T=1、short APDU level exchange です。chaining、extended APDU、
  PIN pad / secure entry、mechanical slot、clock / data rate の交渉は範囲外で、class
  descriptor でも非対応と宣言するため仕様に従う Host は要求しません。CCID メッセージの
  上限は 271 byte です（`ESP_USB_DEVICE_CCID_BUFFER_SIZE` で拡張）。
- 複合デバイスはESP32-S3のUSB endpoint予算とconfiguration descriptor容量で制限されます。
  Audioを含む複合構成はDevice側descriptor制約の確認中です。
- MSC は block device と filesystem を分けて扱います。Host から通常の drive として mount
  するには FAT RAM disk helper または SD card などを使います。
- flash / SPIFFS / LittleFS を USB MSC として直接公開することは標準方針にしません。
- SD card を MSC として Host に公開している間は、ESP32 側で同じ card の file API を使わないでください。
- WebUSB / Microsoft OS 2.0 descriptor はこのライブラリが生成します。WebUSB と
  `USBVendor` を有効にすると、実際に割り当てた vendor interface に対する固定の WinUSB
  compatible ID と device interface GUID を Windows へ返します。custom vendor code、
  GUID、descriptor 内容の差し替え API は未実装です。

USB device そのものの基礎、ESP32 固有の制約、動かないときの切り分け手順は
[docs/usb-device-guide.ja.md](docs/usb-device-guide.ja.md) にまとめています。
TinyUSB との関係、descriptor のバイト構造、callback context、独自 class の実装は
[docs/usb-device-advanced.ja.md](docs/usb-device-advanced.ja.md) にまとめています。
テスト構造と段階的なカバレッジ計画は [tests/TEST_PLAN.ja.md](tests/TEST_PLAN.ja.md)
を参照してください。
設計背景と `EspUsbHost` 既存テストからの移行メモは [docs/DESIGN_NOTES.ja.md](docs/DESIGN_NOTES.ja.md)
にまとめています。
現在の開発方針と残作業は [docs/DEVELOPMENT_PLAN.ja.md](docs/DEVELOPMENT_PLAN.ja.md)
にまとめています。
リリース前確認は [docs/RELEASE_CHECKLIST.ja.md](docs/RELEASE_CHECKLIST.ja.md) を参照してください。
