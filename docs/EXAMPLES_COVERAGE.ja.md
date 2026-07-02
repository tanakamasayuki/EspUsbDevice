# Examples Coverage

この文書は、Arduino-ESP32 3.3.10 bundled USB examples と `EspUsbDevice` examples の対応状況を整理します。

比較対象:

```text
~/.arduino15/internal/esp32_esp32_3.3.10_8b955bf9809c8ad4/libraries/USB/examples
```

`EspUsbDevice` は Arduino-ESP32 標準 `USB` API との互換を目標にしません。ここでの比較は、
ユーザーが期待しそうな使い方、example の粒度、helper が必要な複雑箇所を把握するためのものです。

## 対応状況

| Arduino-ESP32 USB example | EspUsbDevice example | 状態 | メモ |
|---------------------------|----------------------|------|------|
| `USBSerial` | `Serial` | 対応済み | CDC ACM。line coding / DTR / RTS callback も説明済み。 |
| `Keyboard/KeyboardMessage` | `Keyboard` | 対応済み | 文字列入力に加え、layout と raw usage を重視。 |
| `Keyboard/KeyboardLogout` | `Keyboard` | 一部対応 | shortcut / macro は raw usage で表現可能。専用 example は未作成。 |
| `Keyboard/KeyboardReprogram` | `Keyboard` | 一部対応 | 危険な自動 keyboard 操作は専用 example にしない方針。 |
| `Keyboard/KeyboardSerial` | `Keyboard` + `Serial` | 一部対応 | serial 入力を keyboard に変換する応用 example は未作成。 |
| `Mouse/ButtonMouseControl` | `Mouse` | 対応済み | button 入力で mouse 操作する応用 example は未作成。 |
| `KeyboardAndMouseControl` | `KeyboardMouse` | 対応済み | composite HID として基本対応。button/serial 操作の応用は未作成。 |
| `ConsumerControl` | `MediaKeys` | 対応済み | volume / mute / play/pause / track 操作。 |
| `SystemControl` | `MediaKeys` | 対応済み | power / standby / wake usage は説明済み。自動送信は避ける。 |
| `Gamepad` | `Gamepad` | 対応済み | axes / hat / buttons。 |
| `CustomHIDDevice` | `CustomHID` | 対応済み | sketch 定義 descriptor と fixed input report。 |
| `HIDVendor` | `VendorHID` | 対応済み | vendor-defined HID input/output/feature report。 |
| `USBMSC` | `MSC` / `MSCFatRamDisk` | 対応済み | raw block I/O と FAT RAM disk helper に分離。 |
| `FirmwareMSC` | なし | 未対応 | MSC 経由 firmware update。優先度は低め。 |
| `MIDI/MidiMusicBox` | `MIDI` | 一部対応 | note sequence の基本は対応。曲 example は未作成。 |
| `MIDI/MidiController` | `MIDIController` | 対応済み | ADC -> CC、button -> Note。 |
| `MIDI/MidiInterface` | `MIDIInterface` | 対応済み | UART MIDI 31250 baud と USB MIDI の bridge。 |
| `MIDI/MidiMultiChannel` | `MIDI` | 一部対応 | channel 指定 API はある。専用 multi-channel example は未作成。 |
| `MIDI/ReceiveMidi` | `MIDI` | 対応済み | `readPacket()` の受信ログあり。 |
| `USBVendor` | `USBVendor` | 一部対応 | HID ではない vendor interface / bulk IN/OUT / control request / WebUSB URL。custom vendor code と Microsoft OS 2.0 descriptor 差し替え API は未実装。 |
| `AudioCard` | `AudioSink` | 一部対応 | 最小 speaker sink。I2S bridge / codec はこのライブラリの責務外。 |
| `CompositeDevice` | 複数 example に分割 | 一部対応 | CDC + HID + MSC + Vendor 全部入りは未作成。 |

## 現在の EspUsbDevice examples

| Example | 目的 | 優先度 |
|---------|------|--------|
| `Keyboard` | HID keyboard、layout、raw usage、LED output report | 高 |
| `Mouse` | HID mouse、movement、wheel、buttons | 高 |
| `KeyboardMouse` | keyboard + mouse composite HID | 高 |
| `Gamepad` | axes / hat / buttons | 中 |
| `MediaKeys` | consumer/system control | 中 |
| `VendorHID` | vendor-defined HID report exchange | 中 |
| `USBVendor` | vendor-specific interface、bulk IN/OUT、control request | 中 |
| `CustomHID` | sketch-defined HID descriptor | 中 |
| `Serial` | CDC ACM serial | 高 |
| `MIDI` | USB MIDI basic send/receive | 中 |
| `MIDIController` | ADC/button to MIDI | 中 |
| `MIDIInterface` | UART MIDI <-> USB MIDI bridge | 中 |
| `MSC` | raw MSC block I/O | 低 |
| `MSCFatRamDisk` | FAT RAM disk file handoff | 低 |
| `MSCSdCard` | SD card as USB storage | 低-中 |

MSC は準備コストが高く、利用頻度も HID / CDC / MIDI より低いため、追加強化の優先度は下げます。
現状は helper と手動確認手順を用意し、実機確認待ちにします。

## 不足と次候補

### USBVendor / WebUSB

公式 `USBVendor` は `VendorHID` とは別物です。

- vendor-specific interface
- bulk IN / bulk OUT endpoint
- vendor request callback
- WebUSB landing URL
- control request handling
- stream-like write / flush

`EspUsbDevice` では `EspUsbDeviceVendor` として実装します。
HID report ではなく class/vendor interface descriptor と control transfer を扱います。
公式 Arduino-ESP32 `USBVendor` API との互換は目標にせず、`EspUsbDevice` の使い方に合わせて
単純な stream + callback API にします。

実装予定の段階:

1. 最小実装: 対応済み
   - vendor-specific interface (`bInterfaceClass = 0xff`)。
   - bulk IN / bulk OUT endpoint。
   - `begin()` / `available()` / `read()` / `write()` / `flush()` 相当の stream API。
   - control request callback。`bmRequestType`、`bRequest`、`wValue`、`wIndex`、`wLength` を
     user callback に渡し、Device 情報取得や mode 切替に使えるようにする。
   - unit descriptor test と build-only example。
2. WebUSB landing URL: 対応済み
   - `EspUsbDeviceConfig::webusbEnabled` / `webusbUrl` で Arduino-ESP32 TinyUSB core の
     WebUSB BOS / URL descriptor を有効化する。
   - `peer/usb_vendor` で Host から landing URL を読み出す自動テストを追加済み。
   - browser / libusb / WinUSB での確認は `tests/manual` の手順で扱う。
3. Windows 用 descriptor: 一部対応
   - Arduino-ESP32 TinyUSB core は WebUSB 有効時に Microsoft OS 2.0 descriptor も返す。
   - `EspUsbDevice` 側で GUID や descriptor 内容を差し替える API は未実装。

`VendorHID` は HID report 経由の簡易独自通信として維持します。`EspUsbDeviceVendor` は
HID report サイズや HID driver の制約を避けたい場合、PC app / browser / bulk transfer を
使いたい場合のために追加します。

### USB Audio

公式 `AudioCard` は I2S bridge を含む本格 example です。このライブラリでは USB Audio と
PCM callback 境界だけを扱い、I2S bridge / codec / DAC 接続は PCMFlowDevice など出力側に任せます。

- USB Audio descriptor
- isochronous endpoint
- speaker output callback
- microphone input write
- volume / mute / sample rate / alternate setting
- PCM callback boundary

`AudioSink` で単独 device の最小 speaker sink を追加済みです。複合 Audio device は未対応です。
PCMFlow 連携は有力な利用形ですが、汎用 callback I/F を維持して特定ライブラリへの必須依存にはしません。

### FirmwareMSC

MSC 経由 firmware update は、`EspUsbDeviceMscFatRamDisk` と組み合わせる方向が自然です。

- Host が `firmware.bin` を FAT RAM disk に置く。
- eject 後に Device 側で file を読む。
- OTA partition へ書き込む。

ただし firmware update は失敗時の復旧やサイズ制限が重要なため、まず example / docs 先行に留めます。

### Keyboard / Mouse 応用

公式には serial 入力や buttons で keyboard / mouse を動かす応用 example があります。
`EspUsbDevice` では基本 API の example は揃っているため、必要になったら追加します。

候補:

- `KeyboardSerial`: Serial 入力を keyboard typing に変換。
- `ButtonMouseControl`: GPIO buttons で mouse movement / click。
- `KeyboardMacro`: raw usage で shortcut を送信。

### MIDI 応用

`MIDIController` と `MIDIInterface` を追加済みです。必要なら以下を追加します。

- `MIDIMultiChannel`: 複数 channel に note / CC を送る。
- `MIDIMusicBox`: note sequence / tempo helper の example。

## Helper 候補

| Helper | 理由 | 優先度 |
|--------|------|--------|
| `EspUsbDeviceVendor` | bulk IN/OUT + control request + WebUSB URL は追加済み。custom vendor code / Microsoft OS 2.0 descriptor 差し替え API が残項目。 | 中 |
| `EspUsbDeviceAudioSink` | 最小 sink は追加済み。PCMFlow などへ渡しやすい callback I/F の整理が残項目。 | 中 |
| MIDI serial parser helper | `MIDIInterface` の SysEx / running status / realtime 対応。 | 低-中 |
| Firmware handoff helper | FAT RAM disk 上の `firmware.bin` を安全に扱う。 | 低 |
| Keyboard macro helper | shortcut / modifier sequence を読みやすくする。 | 低 |
| Button/debounce helper | examples 用。library 本体より example-local が適切。 | 低 |

## Compile Smoke

examples は手動で compile smoke を実行できます。

```sh
cd tests
uv run --env-file .env pytest examples_compile/ -vv
```

現在のリリース範囲では 15 examples を compile smoke の対象にします。
