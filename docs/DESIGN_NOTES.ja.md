# EspUsbDevice 設計メモ

この文書は、新規 USB Device ライブラリ `EspUsbDevice` の設計背景、初期実装方針、`EspUsbHost` 既存テストからの移行メモをまとめたものです。

`EspUsbDevice` の最終目的は、ESP32 Arduino 向けによりよい USB Device ライブラリを提供することです。
初期段階では `EspUsbHost` の peer / loopback テストを優先ターゲットにしますが、これは実ハードウェアで低レベル挙動を具体的に検証しながら API と実装を固めるためです。

## 背景

現在の `EspUsbHost` テストは、ホスト側をこのライブラリで実装し、ピアデバイス側を Arduino-ESP32 標準の USB Device ライブラリで実装しています。
既存の peer テストは一通り動いていますが、テスト対象を広げるほど Arduino-ESP32 側の USB Device API と実装に限界が出ています。

特に問題になっている点は以下です。

- ESP32-P4 の Arduino USB Device 実装は High Speed 固定で、Full Speed device として起動する標準 API がない。
- P4 の `USB.begin()` / TinyUSB 初期化は rhport と PHY speed を P4 向けに固定しており、loopback テストで FS host 側に接続できない。
- HID keyboard API は US 配列・ASCII 入力寄りで、JIS/日本語キーボード固有キーや raw HID usage を正確に扱う設計になっていない。
- HID output report / feature report / LED など、テストに必要な低レベル挙動を安定して制御しづらい。
- Arduino-ESP32 本体へ PR するには、USB core、TinyUSB config、descriptor、board menu、後方互換性を横断して修正する必要があり、単発修正では済まない。

そのため、`EspUsbHost` と同じ思想で USB Device 側も明示的に制御できる `EspUsbDevice` を別ライブラリとして作る方針にしたいです。

## 目標

`EspUsbDevice` は、Arduino-ESP32 標準の `USB` / `USBHIDKeyboard` 互換ライブラリではなく、USB device の低レベル挙動を明示的に制御できるライブラリとして設計します。
最初は `EspUsbHost` のテスト・検証・サンプル用途に必要な機能から実装します。

主な目標は以下です。

- USB device の port、speed、descriptor、endpoint MPS を明示的に制御する。
- Peer テストと P4 loopback テストの両方で、ホスト・デバイスを EspUsb シリーズだけで構成できるようにする。
- HID は文字入力 API より raw report / HID usage を第一級 API として扱う。
- JIS/日本語キーボードを US 配列の拡張ではなく、HID usage とレイアウト変換の分離で扱う。
- Arduino-ESP32 標準 USB Device stack とは排他利用にする。
- 既存 `EspUsbHost` のテストを、Arduino-ESP32 device API 依存から段階的に移行できるようにする。

## 非目標

初期段階では以下を目標にしません。

- Arduino-ESP32 標準 `USBHIDKeyboard` / `USBHIDMouse` API との完全互換。
- すべての USB class を最初から網羅すること。
- 初期段階から PC 用の一般的な USB Device ライブラリとして完成させること。
- Arduino-ESP32 core の `USB.begin()` と同時利用すること。
- USB Hub、複数段 topology、OTG role switch まで含む汎用 USB stack を作ること。

## 既存 Arduino-ESP32 USB Device 実装の制約

### ESP32-P4 device speed

Arduino-ESP32 3.3.10 の P4 TinyUSB device 実装は、P4 の場合に PHY と TinyUSB を High Speed 固定で初期化しています。

代表的な実装は以下です。

```cpp
#if CONFIG_IDF_TARGET_ESP32P4
  .otg_speed = USB_PHY_SPEED_HIGH,
#else
  .otg_speed = USB_PHY_SPEED_FULL,
#endif
```

```cpp
#if CONFIG_IDF_TARGET_ESP32P4
  tinit.speed = TUSB_SPEED_HIGH;
  tusb_init(1, &tinit);
#else
  tinit.speed = TUSB_SPEED_FULL;
  tusb_init(0, &tinit);
#endif
```

また `tusb_config.h` 相当でも P4 は HS 最大速度です。

```cpp
#if CONFIG_IDF_TARGET_ESP32P4
#define CFG_TUD_MAX_SPEED OPT_MODE_HIGH_SPEED
#else
#define CFG_TUD_MAX_SPEED OPT_MODE_FULL_SPEED
#endif
```

endpoint size も High Speed 前提で 512 になります。

```cpp
#define CFG_TUD_ENDPOINT_SIZE (TUD_OPT_HIGH_SPEED ? 512 : 64)
```

このため、P4 で Arduino 標準 `USBHIDKeyboard` を使うと、HID endpoint でも `wMaxPacketSize=512` が出る場合があります。
P4 の FS host 側でこれを受けると、以下のように endpoint allocation で失敗します。

```text
HCD DWC: EP MPS (512) exceeds supported limit (128)
USBH: EP Alloc error: ESP_ERR_NOT_SUPPORTED
USB HOST: Claiming interface error: ESP_ERR_NOT_SUPPORTED
```

### P4 host 側との違い

`EspUsbHost` は Arduino-ESP32 の USB class を使わず、ESP-IDF USB Host API を直接呼んでいます。
P4 では `usb_host_config_t.peripheral_map` により HS host peripheral と FS host peripheral を選択できます。

```cpp
case ESP_USB_HOST_PORT_HIGH_SPEED:
  return 1U << 0;
case ESP_USB_HOST_PORT_FULL_SPEED:
  return 1U << 1;
```

ただし、これは host peripheral の選択であって、「HS host を FS-only で起動する」設定ではありません。
HS host は FS device と FS signaling で通信できる可能性がありますが、controller としては HS host 側です。
SDK に HS Host では USB Hub が使えない制約がある場合、HS host に FS device を接続してもその制約は回避できません。

### P4 USB ポート/PHY の実測整理（2026-07 実機確認）

`loopback/usb_audio` の切り分けで P4 の USB ポート/PHY の実態を確認したので、正確な内容をここに固定します。
以前の「P4 は FS device として起動できず FS host 側 loopback に接続できない」という理解は不正確でした。

- P4 は **OTG コントローラが2個、UTMI(HS) PHY は1個だけ**（`SOC_USB_OTG_PERIPH_NUM=2`, `SOC_USB_UTMI_PHY_NUM=1`）。
- **速度とコントローラ/PHY は別概念。** HS 対応コントローラ(UTMI)でも、接続相手が FS なら FS にネゴして動く。
- `EspUsbDevice` のデバイスは Arduino core により **HS(UTMI) PHY 固定**。`EspUsbDeviceConfig.port` / `speed` は
  `tinyusb_init` に渡っておらず（[EspUsbDevice.cpp](../src/EspUsbDevice.cpp) の `tinyusb_device_config_t` に該当欄が無い）、
  デバイス側からポート/速度は選べない。`config_.port` / `config_.speed` は現状ほぼ未使用。
- したがってデバイスは「**HS PHY 上で FS 動作**」が可能で、FS host には問題なく enumerate できる（過去の「接続できない」は誤り）。
- 1台 loopback では UTMI PHY が1個しかなく、デバイスがそれを握る。**ホスト側は FS 側ポート（`ESP_USB_HOST_PORT_FULL_SPEED`）に限定**される。
  ホストを HS にすると同じ UTMI PHY を奪い合い、`usb_phy: selected PHY is in use` で失敗する。
- 帰結: **1台 P4 loopback で HS リンクは作れない**（HS PHY を host / device で共有できない）。HS device + HS host の loopback は不可で、HS リンクの検証には2台構成が要る。

| 役割 | 載る PHY/コントローラ | 動作速度 | 備考 |
|------|----------------------|----------|------|
| Device | HS(UTMI) 固定 | 相手依存（FS host なら FS に落ちる） | core が P4 で決め打ち。ライブラリからは変更不可 |
| Host | port で選択（FS=別 PHY / HS=UTMI） | port 依存 | 1台 loopback では FS 必須 |

#### USB Audio への影響と決定（P4 Audio は HS 専用）

1台 loopback のリンクは FS だが、audio descriptor は**コンパイル時マクロ** `TUD_OPT_HIGH_SPEED`（P4 は 1）で
UAC2 として生成される（[EspUsbDeviceAudio.cpp](../src/EspUsbDeviceAudio.cpp)）。UAC2 は実質 HS 向けで、`EspUsbHost` の audio
パーサも UAC1(Audio 1.0) のみ対応。よって FS リンクの loopback では P4 Audio を正しく流せない。

「実リンク速度に追従して UAC1/UAC2 を出し分ける」ことは、このスタックでは現実的に不可能と確認した:

- config descriptor は接続前に `tinyusb_init()` で**一度だけ**組まれる（`tinyusb_load_enabled_interfaces()`）。
  `tud_descriptor_configuration_cb` は固定バッファを返すだけで、生成時点で `tud_speed_get()` は無効。
- しかも descriptor を組む主体が2つに分かれている（HID/CDC 等は EspUsbDevice の `tinyusb_enable_interface`、
  **Audio は esp_tinyusb コンポーネントの `tusb_audio_load_descriptor`**）。組み上がったバッファは core の
  static で外から触れない。request 時に速度別再生成するには両サブシステムを自前で握る＝実質フォークが必要。

**決定：P4 の Audio は UAC2 / HS 専用とする**（コード変更なし。すでにそう動作している）。S3 は FS/UAC1 で
`peer/usb_audio_speaker` が自動カバー、P4 の UAC2/HS は実機 HS 手動確認でカバーする。1台 P4 loopback は FS 限定で
Audio と噛み合わないため、**loopback の audio テストは置かない**（`tests/loopback/usb_audio` は削除）。
将来 UAC2 を自動化したい場合は P4 2台の HS peer 構成で行う。

#### bulk エンドポイントサイズと HS 準拠（現状の既知制約）

Audio と同根の問題が bulk エンドポイントにもある。

- 現状、EspUsbDevice の endpoint サイズは全クラス **FS 値で固定**：HID interrupt=8（`hidEndpointSize()` が 8 を返す）、
  CDC / MIDI / MSC / Vendor の bulk=64（各 loader にベタ書き）。
- これは S3(FS) と P4 1台 loopback(FS) では正しいが、**P4 を実 HS ホスト(PC)に接続すると bulk EP が非準拠**になる。
  USB2.0 では HS の bulk は `wMaxPacketSize=512` 必須なのに 64 を出すため。寛容なホストでは動くこともあるが規格違反。
- 逆に P4 で bulk=512 に決め打ちすると、今度は **FS（loopback や FS ホスト）で 512 が無効**になり、FS ホストが
  EP を割り当てられず enumerate に失敗する（＝標準 `USBHIDKeyboard` が P4 loopback で落ちたのと同じ現象）。
  つまり片側決め打ちでは HS/FS の一方が必ず非準拠になる。
- 完全な正解は **per-speed descriptor**：`tud_descriptor_configuration_cb` を自前所有し、`tud_speed_get()` で
  FS=64 / HS=512 を出し分け、`device_qualifier` / `other_speed_configuration` も用意する。ただし core は config
  descriptor を接続前に一度だけ組み（`tinyusb_load_enabled_interfaces()`）、audio は esp_tinyusb が別途組むため、
  これは descriptor 経路の自前引き取り＝相応の作業量＋P4 実機反復が必要。

**現段階の方針（決定）**：FS loopback を主対象とするため **bulk=64 を許容**し、HS 非準拠は既知の制約として受け入れる。
実 PC/HS 対応を正式ターゲットにするマイルストーンで、per-speed descriptor 化とセットで対応する（その際は
bulk クラスの loopback テストを消さずに HS/FS 両対応にできる）。影響範囲は CDC / MIDI / MSC / Vendor（bulk）。
HID 系は interrupt EP（小サイズは HS でも有効）なので影響なし。

### HID keyboard API の限界

Arduino 標準 keyboard API は `write(char)` のような文字入力 API が中心です。
これは US 配列の簡易入力には便利ですが、テスト用 device としては以下が問題です。

- HID usage ID と文字変換が混ざる。
- JIS 固有キーを扱いづらい。
- `無変換`、`変換`、`かな`、`半角/全角`、`ろ`、JIS の `¥` などが自然に表現できない。
- raw boot keyboard report を任意に送るテストが書きづらい。
- output report / LED の受信確認が Arduino 標準 API の実装都合に引きずられる。

`EspUsbDevice` では、文字 API は上位レイヤとして扱い、基本は raw HID usage / report を直接扱うべきです。

## 設計方針

### 1. Arduino USB stack とは排他

`EspUsbDevice` を使うスケッチでは、Arduino-ESP32 標準の以下を使わない前提にします。

- `USB.begin()`
- `USBHIDKeyboard`
- `USBHIDMouse`
- `USBCDC`
- `USBMSC`
- `USBMIDI`
- `USBAudioCard`

同じ TinyUSB / USB PHY / endpoint を二重初期化すると破綻するため、排他利用を明文化します。

### 2. Device 初期化を明示設定にする

```cpp
struct EspUsbDeviceConfig {
  const char *manufacturer = "EspUsbDevice";
  const char *product = "EspUsbDevice";
  const char *serialNumber = nullptr;
  uint16_t vid = 0x303a;
  uint16_t pid = 0x4000;
  bool selfPowered = false;
  uint16_t maxPowerMilliamps = 100;
};
```

> 【2026-07 改訂 / Step 1】当初は `EspUsbDevicePort` / `EspUsbDeviceSpeed` で port / speed を
> "選択" できる設計にしていたが、これは誤った抽象だったため撤去した。理由:
>
> - **port は honor できない。** P4 では Arduino core がデバイスを HS(UTMI) コントローラに
>   決め打ちしており（`init_usb_hal` / `tusb_init(1)`）、`config` から選べない（実際 `config_.port` /
>   `config_.speed` はどこにも使われていなかった）。
> - **speed は"選択"するものではない。** 実際のリンク速度はホストとのネゴで決まる。デバイスが宣言した
>   値で EP サイズを決めると、ネゴ結果と食い違ったとき壊れる（HS 決め打ちの Core は FS で EP512 が
>   通らず、FS 決め打ちだと HS で bulk=512 必須に違反する）。
>
> よって capability（FS-only か HS 対応か）は**ハードで決まる前提**とし、config からは port / speed の
> 選択欄を無くした。理想は EP サイズや UAC1/UAC2 を**ネゴ速度に追従して出し分ける**ことだが、このスタック
> では build-once 等の制約で実質フォークが必要と判明したため見送り、現段階は全 class とも **FS サイズ固定**
> （HS 非準拠は既知制約）とする。詳細は「P4 USB ポート/PHY の実測整理」および「bulk エンドポイントサイズと
> HS 準拠」を参照。

### 3. Descriptor はライブラリ側で所有する

Arduino-ESP32 の共通 `CFG_TUD_ENDPOINT_SIZE` に依存せず、device speed と class ごとに適切な MPS を生成します。

目安:

- FS interrupt endpoint: 8/16/32/64 bytes。HID keyboard/mouse なら 8 bytes 程度でよい。
- FS bulk endpoint: 64 bytes。
- HS bulk endpoint: 512 bytes。
- HS interrupt endpoint: class/report に応じて明示。HID keyboard で 512 にする必要はない。
- EP0: 基本 64 bytes。

テスト用途では「ホストが正しく扱うべき descriptor」を作ることが重要です。
速度に応じた endpoint MPS の正しさをユニットテスト対象にします。

> 【現状 / 2026-07】上記は目標。実装は**全 class とも FS サイズ固定**（interrupt=8、bulk=64）で、
> 速度別の出し分けはまだ入れていない。P4 を実 HS ホストに繋ぐと bulk が非準拠になる既知制約がある。
> 詳細と方針は「bulk エンドポイントサイズと HS 準拠」を参照。

### 4. Class は小さい部品として合成する

Device 全体を `EspUsbDevice` が管理し、各 class は追加登録する形にします。

```cpp
EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);
EspUsbDeviceHidMouse mouse(device);
EspUsbDeviceCdcAcm cdc(device);

void setup() {
  EspUsbDeviceConfig config;
  device.begin(config);
}
```

Composite device を自然に作れることが重要です。
Peer テストでは keyboard + mouse、HID vendor in/out、CDC、MIDI、MSC などを組み合わせます。

### 5. raw report を第一級 API にする

HID keyboard の最小 API は文字入力ではなく report 送信です。

```cpp
struct EspUsbDeviceBootKeyboardReport {
  uint8_t modifiers;
  uint8_t reserved;
  uint8_t keys[6];
};

keyboard.sendReport(report);
keyboard.pressUsage(ESP_USB_HID_KEY_A);
keyboard.releaseUsage(ESP_USB_HID_KEY_A);
keyboard.releaseAll();
```

文字入力は別レイヤです。

```cpp
keyboard.setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_US);
keyboard.write("abc");
keyboard.setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_JA_JP);
keyboard.write("@[]:\"");
```

layout ID は Host 側の `EspUsbHostKeyboardLayout` と同じ値にします。
Device 側は `ascii -> usage/modifier`、Host 側は `usage/modifier -> ascii` の
逆方向変換として対応させます。初期実装では `EN_US` と `JA_JP` の ASCII wrapper を
提供し、他の layout は Host 側 keymap と同じ粒度で順次追加します。

JIS キーは ASCII ではなく usage として明示します。

```cpp
keyboard.pressUsage(ESP_USB_HID_KEY_LANG1);       // kana 等
keyboard.pressUsage(ESP_USB_HID_KEY_INTERNATIONAL1);
keyboard.pressUsage(ESP_USB_HID_KEY_INTERNATIONAL3);
keyboard.pressUsage(ESP_USB_HID_KEY_HENKAN);
keyboard.pressUsage(ESP_USB_HID_KEY_MUHENKAN);
```

名称は HID Usage Tables に合わせ、ローカル別名を足す場合でも usage 値との対応を明示します。

## 必要機能

### MVP

最初に作るべき最小範囲です。

- P4/S3 のビルド対応。
- Arduino 標準 USB stack と排他で TinyUSB device を初期化。
- device port / speed / VID / PID / string descriptor の設定。
- descriptor 生成と endpoint MPS の speed 別切替。
- HID keyboard boot protocol device。
- raw keyboard report 送信。
- keyboard output report 受信 callback。NumLock/CapsLock/ScrollLock を検証可能にする。
- HID mouse boot protocol device。
- raw mouse report 送信。
- pytest-embedded の peer device として使える serial command protocol。

MVP の目的は、既存 `tests/peer/hid_keyboard`、`tests/peer/hid_mouse`、`tests/peer/hid_keyboard_mouse`、P4 `loopback/hid_keyboard` を Arduino 標準 USB device なしで再実装することです。

### HID 拡張

次に必要な HID 機能です。

- HID consumer control。
- HID system control。
- HID gamepad。
- HID vendor IN/OUT/Feature。
- custom HID report descriptor 登録。
- report descriptor 取得テスト用の安定した descriptor。
- report ID あり/なしの両方。
- output report / feature report の送受信。
- boot protocol / report protocol 切替への対応。

既存 Host 側テストとの対応:

- `peer/hid_consumer_control`
- `peer/hid_system_control`
- `peer/hid_gamepad`
- `peer/hid_vendor`
- `peer/custom_hid`
- `peer/hid_logic`

### CDC ACM

必要機能:

- CDC ACM composite interface。
- device to host 送信。
- host to device 受信。
- line coding / control line state callback。
- baudrate、parity、stop bits、data bits の設定イベントをテスト可能にする。

既存 Host 側テストとの対応:

- `peer/usb_serial`

### USB MIDI

必要機能:

- MIDI streaming interface。
- USB MIDI event packet の raw send/receive。
- note on/off、control change、program change、pitch bend、channel pressure、poly pressure。
- SysEx 送受信。

既存 Host 側テストとの対応:

- `peer/usb_midi`

### USB Mass Storage

必要機能:

- BOT MSC device。
- 1 LUN から開始し、後で複数 LUN。
- block size 512。
- inquiry / capacity / test unit ready / request sense。
- read10/write10。
- out-of-range rejection。
- write failure injection。
- synchronize cache。
- removable / writable flag。

既存 Host 側テストとの対応:

- `peer/usb_msc`

MSC は実装量が大きいので、HID/CDC/MIDI の後に着手してよいです。

### USB Audio

必要機能:

- 最初は Audio output sink として、host からの speaker stream を受ける。
- sample rate、format、channel count を限定してよい。
- `onPcm()` callback で PCM と sample rate / channel count / sample width を受け取る。
- `onData()` は buffer と length だけを扱う低レベル callback として残す。
- このライブラリの責務は USB Audio class と PCM callback 境界までに限定する。
- 受け取った PCM はアプリケーション、PCMFlow、PCMFlowDevice などへ渡す。I2S、codec、DAC などの
  出力デバイス接続はこのライブラリでは扱わない。

既存 Host 側テストとの対応:

- `peer/usb_audio_speaker`
- `peer/usb_audio_microphone`

Audio speaker sink は最小実装と peer test（S3, UAC1/FS）まで追加済みです。P4 の Audio は UAC2/HS 専用で、
1台 loopback（FS 限定）とは噛み合わないため **loopback の audio テストは置かない**方針に決定
（上記「USB Audio への影響と決定」参照）。残作業は、P4 UAC2/HS の実機 HS 手動確認、M5 speaker 実音確認、
microphone path、複合 Audio device の制約確認です。

## テスト計画

### テスト体系

新リポジトリでは、最終的に以下の構成を目指します。

```text
tests/
  unit/       descriptor、HID usage、report builder などホスト不要の単体テスト
  peer/       2台構成。Host は EspUsbHost、Device は EspUsbDevice
  loopback/   P4 1台構成。Host も Device も EspUsb 系
  probe/      P4 port / speed / PHY / PC enumeration の切り分け
  manual/     物理デバイスや目視確認が必要なもの
```

### Peer テスト

目的は、既存 Arduino-ESP32 device peer を `EspUsbDevice` に置き換えることです。

初期移行順:

1. `hid_keyboard`
2. `hid_mouse`
3. `hid_keyboard_mouse`
4. `custom_hid`
5. `hid_vendor`
6. `hid_consumer_control`
7. `hid_system_control`
8. `hid_gamepad`
9. `usb_serial`
10. `usb_midi`
11. `usb_msc`
12. `usb_audio_speaker`
13. `usb_audio_microphone`

各 peer device sketch は serial command で挙動を制御します。
例:

```text
SEND_KEY_USAGE 04
SEND_KEY_TEXT hello
SEND_MOUSE_MOVE 40 0 0
SEND_VENDOR_IN hello
EXPECT_VENDOR_OUT
FAIL_NEXT_MSC_WRITE
```

Python 側は既存と同じ `peers["device"]` を使い、host 側のシリアル出力を検証します。

### Loopback テスト

P4 1台で host と device を同時に起動するテストです。
既存の Arduino USB Device では P4 device が HS 固定になり、FS host 側 loopback が失敗しました。
`EspUsbDevice` では P4 の port/speed を明示し、以下を検証します。

- FS リンクの loopback（device は HS/UTMI PHY 上で FS 動作、host は FS 側）。これが 1台で実現できる唯一の形。
- endpoint MPS が speed と class に対して正しいこと。
- HS device + HS host の loopback は **1台では不可**（UTMI PHY が1個で共有できない）。HS リンクは 2台 peer が必要。詳細は「P4 USB ポート/PHY の実測整理」。

Loopback は最初からすべて通す必要はありません。
まずは device speed と descriptor が意図通りになっていることをログ化できる probe を作り、その後 HID keyboard から自動テスト化します。

> 【メモ / 将来の P4 peer との関係】P4 の HS パス（HS enumerate、bulk=512、UAC2/HS audio 等）は
> loopback では物理的に届かないため、将来は **P4 2台の HS peer**（両側とも無指定で HS になる）を追加して補完するのが自然。
> ただし **P4 peer は2台＋配線が要り維持コストが高く、常時 CI 的には回せない想定**。位置づけは「HS 検証用の時々／手動レイヤー」。
> 一方 **loopback は1台で回せて、P4 FS＋両スタック共存を検証できる主力**として維持する。両者は置き換えでなく補完。

### Probe

P4 は USB port と speed の切り分けが重要です。
以下の probe を用意します。

- `p4_device_fs_probe`: FS device として PC/外部 host に列挙されるか。
- `p4_device_hs_probe`: HS device として PC/外部 host に列挙されるか。
- `p4_host_fs_probe`: FS host peripheral で外部 FS device を列挙できるか。
- `p4_host_hs_probe`: HS host peripheral で外部 HS/FS device を列挙できるか。
- `p4_loopback_matrix_probe`: P4 内で host/device の組み合わせを試し、port、speed、MPS、claim 結果を出力。

## API 案

### Core

```cpp
#include "EspUsbDevice.h"

EspUsbDevice device;

void setup() {
  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4001;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice HID Keyboard";

  if (!device.begin(config)) {
    Serial.printf("DEVICE_BEGIN_FAILED %s\n", device.lastErrorName());
  }
}

void loop() {
  device.task();
}
```

`device.task()` が必要か、内部 task で動かすかは実装方式に合わせて決めます。
`EspUsbHost` と同様に `begin()` 後は内部 task で回す設計でもよいです。

### HID keyboard

```cpp
EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);

void setup() {
  keyboard.onOutputReport([](const EspUsbDeviceHidKeyboardOutputReport &report) {
    Serial.printf("LED num=%u caps=%u scroll=%u\n", report.numLock, report.capsLock, report.scrollLock);
  });

  keyboard.begin();
  device.begin(config);
}

void sendA() {
  keyboard.pressUsage(ESP_USB_HID_KEY_A);
  delay(10);
  keyboard.releaseUsage(ESP_USB_HID_KEY_A);
}
```

### HID custom/vendor

```cpp
EspUsbDeviceHidVendor vendor(device, {
  .reportSize = 64,
  .inputReportId = 1,
  .outputReportId = 2,
  .featureReportId = 3,
});

vendor.onOutputReport([](const uint8_t *data, size_t len) {
  Serial.printf("VENDOR_OUT len=%u\n", (unsigned)len);
});

vendor.sendInput(data, len);
vendor.setFeature(data, len);
```

### CDC ACM

```cpp
EspUsbDeviceCdcAcm cdc(device);

cdc.onLineCoding([](const EspUsbDeviceCdcLineCoding &coding) {
  Serial.printf("CDC_LINE baud=%lu data=%u parity=%u stop=%u\n",
                coding.baud, coding.dataBits, coding.parity, coding.stopBits);
});

cdc.write("hello", 5);
```

### MSC

```cpp
EspUsbDeviceMsc msc(device);

msc.onRead([](uint32_t lba, uint32_t offset, void *buffer, uint32_t size) -> int32_t {
  return storageRead(lba, offset, buffer, size);
});

msc.onWrite([](uint32_t lba, uint32_t offset, const uint8_t *buffer, uint32_t size) -> int32_t {
  return storageWrite(lba, offset, buffer, size);
});

msc.begin({
  .blockCount = 16,
  .blockSize = 512,
  .vendor = "ESPUSB",
  .product = "MSC_PEER",
  .revision = "1.0",
});
```

MSC は block device と filesystem を分けて設計します。`EspUsbDeviceMsc` は SCSI /
READ(10) / WRITE(10) の transport と block I/O callback だけを担当し、RAM、SD、FAT image
などの使いやすさは helper class に分離します。

初期 helper は次の分担にします。

```cpp
static uint8_t storage[256 * 1024];

EspUsbDeviceMsc msc(device);
EspUsbDeviceMscRamDisk blocks(storage, sizeof(storage) / 512);

blocks.attach(msc);
```

`EspUsbDeviceMscRamDisk` は raw block I/O、peer / loopback テスト、低レベル MSC example
向けです。FAT は生成しないため、PC が通常の USB drive として mount できるとは限りません。

ファイル受け渡し用途には `EspUsbDeviceMscFatRamDisk` を追加する方針です。

```cpp
static uint8_t storage[256 * 1024];

EspUsbDeviceMsc msc(device);
EspUsbDeviceMscFatRamDisk disk(storage, sizeof(storage));

disk.volumeLabel("ESPUSB");
disk.addTextFile("README.TXT", "Drop firmware.bin and eject.\r\n");
disk.attach(msc);

disk.onEject([]() {
  if (disk.exists("FIRMWARE.BIN")) {
    // firmware update or Wi-Fi upload
  }
});
```

`EspUsbDeviceMscFatRamDisk` の最初の仕様は、実用範囲を意図的に小さくします。

- 512 byte sector 固定。
- FAT12 または小容量 FAT16 の最小 image を生成する。
- long file name は扱わず、8.3 filename を標準にする。
- root directory 直下の通常 file を対象にする。
- directory、timestamp、attribute の高度な操作は後回しにする。
- Host 書き込み中は ESP32 側で FAT を読まない。
- `SYNCHRONIZE CACHE(10)`、eject、`START STOP UNIT` 後に file scan / read を行う。
- firmware update や Wi-Fi 転送では、RAM 上に全保持する方式から始める。
- 大きな file は後で streaming / PSRAM / SD へ拡張する。

永続ストレージ用途には `EspUsbDeviceMscSdCard` を追加する方針です。SD は元から block
device で、USB MSC と FAT の相性がよいため、ユーザー向けの実用 example に向いています。
ただし Host が MSC として SD を所有している間は、ESP32 側が同じ filesystem を同時に
mount / 書き込みしない排他設計にします。eject / stop 後に ESP32 側へ所有権を戻します。

初期実装では Arduino-ESP32 の SPI `SD` を対象にし、`SDFS::readRAW()` /
`SDFS::writeRAW()` を使います。Arduino の通常 file API ではなく、sector 単位の raw I/O
として扱うことが重要です。

```cpp
#include <SD.h>
#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceMsc msc(device);
EspUsbDeviceMscSdCard sdMsc(SD);

void setup() {
  sdMsc.begin(SD_CS, SPI, 4000000);
  sdMsc.onEject([]() {
    // Host ownership ended. Device-side file access may resume here.
  });
  sdMsc.attach(msc);
  msc.mediaPresent(true);
  msc.isWritable(true);
  device.begin(config);
}
```

`EspUsbDeviceMscSdCard` の最初の仕様は以下にします。

- Arduino `SD` / SPI 接続から始める。
- sector size は 512 bytes のみ対応する。
- MSC の offset 付き read/write は内部で sector read-modify-write する。
- `readOnly(true)` で Host write を拒否できる。
- Host 所有中は ESP32 側 file API を使わないことを docs / example で強調する。
- `SD_MMC` 対応は、同じ raw sector API で扱えることを確認してから追加する。

内蔵 flash、SPIFFS、LittleFS を USB MSC として直接公開する標準 API / example は作りません。
USB MSC は sector-level block device であり、SPIFFS / LittleFS は ESP32 側 filesystem API
なので抽象度が合いません。内蔵 flash は firmware partition、erase block、書き換え耐性の
制約も強いため、一般ユーザー向け導線から外します。

## 実装上の注意

### TinyUSB 統合

実装方式は以下のどちらかを検討します。

1. Arduino-ESP32 bundled TinyUSB を直接使う。
2. 必要なら IDF/TinyUSB の低レベル初期化を独自に行い、Arduino core の `USB.begin()` 経路を使わない。

重要なのは、Arduino core の `esp32-hal-tinyusb.c` にある P4 HS 固定初期化に乗らないことです。
FS device 起動を実現するには、PHY speed、TinyUSB rhport、TinyUSB speed、descriptor MPS をすべて一貫させる必要があります。

### descriptor と runtime speed

High Speed capable device は、単に HS descriptor を出すだけでは不十分です。
実際に FS で接続された場合に FS configuration descriptor を返す必要があります。
TinyUSB の callback と speed 判定、または separate descriptor table の扱いを実装前に確認してください。

`CFG_TUD_ENDPOINT_SIZE` のようなグローバル固定値に class descriptor を依存させると、今回と同じ問題が再発します。
各 endpoint の MPS は device config と接続 speed から決めるべきです。

### P4 の HS/FS port

P4 は host 側では HS peripheral と FS peripheral を `peripheral_map` で選べます。
Device 側でも同等の制御が可能か、ESP-IDF の PHY / TinyUSB port 初期化を確認してください。
Arduino-ESP32 標準 API はこの選択を公開していません。

### エラーログ

テストしやすさのため、初期化と enumeration 周辺では以下を出力できるようにします。

- selected port
- requested speed
- actual TinyUSB rhport
- actual connected speed
- descriptor endpoint MPS
- VID/PID
- interface count
- endpoint list
- last error name

P4 loopback では、このログが原因切り分けに直結します。

## 既存テストからの移行メモ

### `peer/hid_keyboard`

現状は peer device が `USBHIDKeyboard` を使っています。
LED テストは Arduino-ESP32 側の `USBHID.cpp` 実装都合で自動テストから除外されています。
`EspUsbDevice` では keyboard output report callback を実装し、NumLock/CapsLock/ScrollLock の自動テストを復活させます。

### `peer/hid_keyboard_mouse`

Composite HID の基本テストです。
`EspUsbDevice` では keyboard と mouse を別 interface にするか、report ID 付き単一 HID interface にするかを選べるとよいです。
Arduino TinyUSB runtime では複数 HID interface の列挙制約が出るため、MVP は report ID 付き単一 HID interface で進めます。

### `peer/custom_hid`

Host 側は report descriptor 取得と raw input dump を検証しています。
Device 側は任意 descriptor と raw report 送信を安定して行う必要があります。
これは `EspUsbDevice` の設計妥当性を見る良い初期テストです。

### `peer/hid_vendor`

Interrupt IN/OUT と Feature report の往復テストに使います。
output report と feature report の callback を分けて実装してください。

### `peer/usb_serial`

CDC ACM の line coding 設定テストがあります。
Arduino 標準 `USBCDC` 依存を置き換えるには、line coding event をテスト用にログ出力できる必要があります。

### `peer/usb_midi`

MIDI event packet の raw 送受信ができれば、上位 helper は後から足せます。
まずは packet 単位の API を作るべきです。

### `peer/usb_msc`

現在のテストはかなり具体的です。
容量、Inquiry、Sense、Read/Write、範囲外拒否、失敗注入まで含みます。
MSC は `EspUsbDevice` の中盤以降のマイルストーンにします。

### `loopback/hid_keyboard`

初期検討では P4 上で `EspUsbHost` と Arduino `USBHIDKeyboard` を同時に使う構成を試しました。
Arduino device 側が HS 固定で MPS 512 を出し、FS host 側で claim できないことが分かったため、
現在は `EspUsbHost` と `EspUsbDeviceHidKeyboard` の組み合わせへ移行しています。

`EspUsbDevice` 側では以下の matrix を明示的に扱います。

| Device | Host | 期待 |
|--------|------|------|
| HS(UTMI) device / FS 動作 | FS host | 1台 loopback の実現構成。デバイスは HS PHY 固定だが FS でネゴ |
| HS device | HS host | **1台 P4 では不可**（UTMI PHY が1個で共有できず PHY 衝突）。HS リンクは2台構成で検証 |
| — | — | デバイス側は HS(UTMI) 固定なので「FS device port を選ぶ」構成は core 改造なしには作れない |

（上表は 2026-07 の実機確認に基づく。詳細は「P4 USB ポート/PHY の実測整理」を参照。）

## リポジトリ構成案

```text
EspUsbDevice/
  library.properties
  README.md
  README.ja.md
  src/
    EspUsbDevice.h
    EspUsbDevice.cpp
    EspUsbDeviceTypes.h
    EspUsbDeviceDescriptors.h
    EspUsbDeviceHid.h
    EspUsbDeviceHidKeyboard.h
    EspUsbDeviceHidMouse.h
    EspUsbDeviceHidVendor.h
    EspUsbDeviceCdcAcm.h
    EspUsbDeviceMidi.h
    EspUsbDeviceMsc.h
  examples/
    HID/EspUsbDeviceKeyboard/
    HID/EspUsbDeviceKeyboardJis/
    HID/EspUsbDeviceMouse/
    HID/EspUsbDeviceCompositeHID/
    HID/EspUsbDeviceHIDVendor/
    CDC/EspUsbDeviceCdcAcm/
    MIDI/EspUsbDeviceMidi/
    MSC/EspUsbDeviceMscRamDisk/
  tests/
    unit/
    peer/
    loopback/
    probe/
```

## マイルストーン

### Milestone 1: HID keyboard/mouse MVP

- Core device 初期化。
- P4/S3 build。
- speed/port config。
- descriptor 生成。
- HID keyboard raw report。
- HID keyboard LED output report callback。
- HID mouse raw report。
- peer `hid_keyboard`、`hid_mouse`、`hid_keyboard_mouse` 移行。

### Milestone 2: P4 loopback

- P4 device FS/HS probe。
- P4 loopback matrix probe。
- loopback `hid_keyboard` を EspUsbHost + EspUsbDevice で再実装。
- endpoint MPS ログとアサーション。

### Milestone 3: HID generalization

- custom HID descriptor。
- vendor HID IN/OUT/Feature。
- consumer/system/gamepad。
- report ID あり/なし。
- peer HID 系を全移行。

### Milestone 4: CDC and MIDI

- CDC ACM。
- line coding/control line state。
- MIDI raw packet。
- MIDI helper。
- peer `usb_serial`、`usb_midi` 移行。

### Milestone 5: MSC and Audio

- MSC RAM disk。
- failure injection。
- multi-block read/write。
- MSC FAT RAM disk helper。
- MSC SD card helper。
- Audio sink。
- `AudioSpeaker` / `AudioSpeakerM5` の manual 確認。
- USB Audio loopback / microphone path / composite Audio の検討。

## 完了条件

初期リリースの完了条件は以下です。

- Arduino 標準 `USB.begin()` を使わずに、EspUsbDevice 単体で HID keyboard/mouse device として列挙できる。
- ESP32-S3 peer device として既存 HID keyboard/mouse テストを置き換えられる。
- ESP32-P4 で device speed と endpoint MPS を明示的にログ化できる。
- P4 loopback で FS device + FS host、または少なくとも HS device + HS host の HID keyboard テストが通る。
- HID output report callback で keyboard LED の自動テストが可能。
- raw HID usage API により JIS 固有キーのテストを設計できる。

## 新リポジトリ作業時の最初の指示案

新しいリポジトリで作業を開始するときは、以下のように指示するとよいです。

```text
EspUsbDevice という Arduino ライブラリを新規作成してください。
この文書 docs/EspUsbDevice_HANDOFF.ja.md を設計仕様として読み、まず Milestone 1 の HID keyboard/mouse MVP を実装してください。
Arduino-ESP32 標準 USB.begin()/USBHIDKeyboard は使わず、TinyUSB/ESP-IDF の device API を直接使う方針で進めてください。
最初のテスト対象は EspUsbHost の peer/hid_keyboard、peer/hid_mouse、peer/hid_keyboard_mouse の置き換えです。
P4 では device port/speed と endpoint MPS をログ出力できるようにしてください。
```
