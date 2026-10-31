# EspUsbDevice のサンプル

> English: [README.md](README.md)

どの例も、それだけで完結したスケッチです。それぞれに README と、Arduino-ESP32 3.3.12 を固定した
`sketch.yaml` があります。作りたいものに近い例の README から読んでください。何を示す例か、
何が必要か、Host 側でどう確かめるかが書いてあります。

## ビルド

`sketch.yaml` に、対応する board を profile として書いています。

| profile | board |
| --- | --- |
| `esp32s3` | ESP32-S3。USB Mode は「USB-OTG (TinyUSB)」（`USBMode=default`） |
| `esp32s2` | ESP32-S2 |
| `esp32p4` | ESP32-P4 revision v1.x（`ChipVariant=prev3`） |
| `esp32p4x` | ESP32-P4 revision v3.1 以降の「P4X」（`ChipVariant=postv3`） |

```sh
arduino-cli compile --profile esp32s3 examples/Serial
arduino-cli upload  --profile esp32s3 --port <port> examples/Serial
python3 tools/build_check.py esp32s3        # その profile を持つ全部の例
```

board の USB OTG の connector を Host につなぎます。log を出す例は、別の debug 接続
（USB-UART か USB Serial/JTAG）の `Serial` を使います。試験対象の USB device と log が同じ cable を
共有しないためです。ESP32-P4 では、特に書いていない限り HS controller を使います。
[P4HighSpeedDevice](P4HighSpeedDevice/) と [P4FullSpeedDevice](P4FullSpeedDevice/) を参照してください。

## HID

| 例 | 内容 | class の文書 |
| --- | --- | --- |
| [Keyboard](Keyboard/) | boot keyboard：文字の入力、layout、usage の直接指定、同時押し、LED の状態 | [HID](../docs/classes/hid.ja.md) |
| [KeyboardNKRO](KeyboardNKRO/) | N キーロールオーバーと boot protocol への切替え | [HID](../docs/classes/hid.ja.md) |
| [Mouse](Mouse/) | boot mouse：移動、wheel、ボタン、ドラッグ | [HID](../docs/classes/hid.ja.md) |
| [KeyboardMouse](KeyboardMouse/) | keyboard と mouse を 1 つの複合 HID interface にまとめる | [HID](../docs/classes/hid.ja.md) |
| [Gamepad](Gamepad/) | 軸、hat switch、ボタン | [HID](../docs/classes/hid.ja.md) |
| [MediaKeys](MediaKeys/) | Consumer Control と System Control | [HID](../docs/classes/hid.ja.md) |
| [CustomHID](CustomHID/) | スケッチで書いた report descriptor | [HID](../docs/classes/hid.ja.md) |
| [VendorHID](VendorHID/) | vendor 定義の Input / Output / Feature report | [HID](../docs/classes/hid.ja.md) |

## Serial、MIDI、vendor bulk

| 例 | 内容 | class の文書 |
| --- | --- | --- |
| [Serial](Serial/) | CDC ACM：line coding、DTR/RTS、break | [CDC](../docs/classes/cdc.ja.md) |
| [CdcConsole](CdcConsole/) | CDC の port を `Stream&` を受け取るコードへ渡す | [CDC](../docs/classes/cdc.ja.md) |
| [SerialMulti](SerialMulti/) | 1 つの device に複数の CDC port | [CDC](../docs/classes/cdc.ja.md) |
| [MIDI](MIDI/) | note と control change の送信、受信 packet の表示 | [MIDI](../docs/classes/midi.ja.md) |
| [MidiEcho](MidiEcho/) | Host がまだ受け取っていない packet を保持する echo | [MIDI](../docs/classes/midi.ja.md) |
| [MIDIController](MIDIController/) | ADC とボタンから MIDI | [MIDI](../docs/classes/midi.ja.md) |
| [MIDIInterface](MIDIInterface/) | UART MIDI（31250 baud）と USB MIDI の中継 | [MIDI](../docs/classes/midi.ja.md) |
| [USBVendor](USBVendor/) | vendor bulk interface、control 要求、WebUSB、WinUSB | [Vendor](../docs/classes/vendor.ja.md) |
| [VendorEcho](VendorEcho/) | 書き切れなかった残りを保持する最小の bulk echo | [Vendor](../docs/classes/vendor.ja.md) |
| [ReconfigureVendor](ReconfigureVendor/) | `end()`・`configure()`・`begin()` で実行中に容量を変える | [Vendor](../docs/classes/vendor.ja.md) |

## ストレージとファームウェア更新

| 例 | 内容 | class の文書 |
| --- | --- | --- |
| [MSC](MSC/) | RAM 上の raw block device | [MSC](../docs/classes/msc.ja.md) |
| [MSCFatRamDisk](MSCFatRamDisk/) | スケッチがファイルを置いた FAT12 の RAM disk | [MSC](../docs/classes/msc.ja.md) |
| [MSCSdCard](MSCSdCard/) | SPI の SD card を USB drive として見せる | [MSC](../docs/classes/msc.ja.md) |
| [FirmwareMSC](FirmwareMSC/) | drive へ `.bin` か `.uf2` をコピーして更新 | [USB 経由の OTA](../docs/ota-over-usb.ja.md) |
| [FirmwareDFU](FirmwareDFU/) | スケッチを動かしたまま `dfu-util` で更新 | [DFU](../docs/classes/dfu.ja.md) |
| [FirmwareHTTP](FirmwareHTTP/) | USB の network 越しに browser から更新 | [USB 経由の OTA](../docs/ota-over-usb.ja.md) |
| [FirmwareCDC](FirmwareCDC/) | serial port と小さな Host script で更新 | [USB 経由の OTA](../docs/ota-over-usb.ja.md) |
| [FirmwareVendor](FirmwareVendor/) | vendor interface と Host script で更新 | [USB 経由の OTA](../docs/ota-over-usb.ja.md) |
| [FirmwareBootMode](FirmwareBootMode/) | スケッチから ROM loader や ROM DFU へ入る | [USB 経由の OTA](../docs/ota-over-usb.ja.md) |

## Audio、Video、その他の class

| 例 | 内容 | class の文書 |
| --- | --- | --- |
| [AudioSpeaker](AudioSpeaker/) | UAC1 の playback、rate・mute・volume の event | [Audio](../docs/classes/audio.ja.md) |
| [AudioPlayback](AudioPlayback/) | 複数の format と rate を持つ UAC2 の playback | [Audio](../docs/classes/audio.ja.md) |
| [AudioMicrophone](AudioMicrophone/) | 生成した音を UAC1 の capture で送る | [Audio](../docs/classes/audio.ja.md) |
| [AudioHeadset](AudioHeadset/) | playback と capture を 1 つの Function で | [Audio](../docs/classes/audio.ja.md) |
| [AudioSpeakerM5](AudioSpeakerM5/)、[AudioMicrophoneM5](AudioMicrophoneM5/)、[AudioHeadsetM5](AudioHeadsetM5/) | 同じことを M5Stack の speaker と microphone で | [Audio](../docs/classes/audio.ja.md) |
| [VideoCamera](VideoCamera/) | 生成した試験パターンを送る UVC camera | [Video](../docs/classes/video.ja.md) |
| [UsbNetwork](UsbNetwork/) | DHCP と web server を持つ CDC-NCM の network device | [NCM](../docs/classes/ncm.ja.md) |
| [SmartCardReader](SmartCardReader/) | card をスケッチで実装した CCID reader | [CCID](../docs/classes/ccid.ja.md) |

## 複合 device と controller

| 例 | 内容 |
| --- | --- |
| [CompositeHidCdcMsc](CompositeHidCdcMsc/) | keyboard、serial port、FAT disk を 1 つの device に |
| [CompositeCdcMidi](CompositeCdcMidi/) | serial port と MIDI を 1 つの device に |
| [MscCdcBlocks](MscCdcBlocks/) | block device と serial port。staging の大きさは FS と HS の両方で有効 |
| [P4HighSpeedDevice](P4HighSpeedDevice/) | ESP32-P4 の HS（480 Mbps）controller |
| [P4FullSpeedDevice](P4FullSpeedDevice/) | ESP32-P4 の FS controller と pin |
| [Info](Info/) | 立ち上げの確認、対話 console、descriptor の表示 |

## 確認していること

どの例も、`sketch.yaml` にある全 profile で compile できます。例が使っているライブラリの動作は、
別の `validation/` の試験で実機確認しています（ESP32-S3 の FS、ESP32-P4 と P4X の FS と HS、
P4X と Linux PC）。詳しくは[試験](../docs/development/testing.ja.md)を参照してください。
例そのものを Windows や macOS の Host で 1 本ずつ動かした確認はまだありません。

EspUsbDevice 2.x から移る場合：2.x の例はすべて同じ名前でここにあります。
[v2 からの移行](../docs/migrating-from-v2.ja.md)を参照してください。
