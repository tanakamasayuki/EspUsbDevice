# EspUsbDevice

> English: [README.md](README.md)

ESP32 Arduino向けの、必要な機能を明示して使うUSB Deviceライブラリです。
DeviceへFunctionを登録し、資源を設定して`begin()`します。
USB workerはライブラリが起動するため、loopからUSB taskを呼ぶ必要はありません。

このブランチは**v3開発版**です。metadataは3.0.0ですが、v3のrelease/tagは公開していません。
TinyUSBのクラス・共通coreを置き換え、容量と転送方式をinstanceごとのAPIへ移しました。
一部の2.x APIには互換性がありません。[移行ガイド](docs/V3_MIGRATION.ja.md)を参照してください。

```cpp
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceCdcSerial console(device);

void setup() {
  EspUsbBulkStreamConfig buffers;
  buffers.rxCapacity = 256;
  buffers.txCapacity = 512;
  buffers.transferBytes = 64;
  if (console.configure(buffers) != ESP_OK) return;
  if (!device.begin()) return;
}

void loop() {
  while (console.available() && console.writeAvailable()) {
    const int value = console.read();
    if (value >= 0) console.write(uint8_t(value));
  }
  delay(1);
}
```

`begin()`は資源を確保してUSBを開始します。そのlifecycleは`started()`、Hostによる構成完了は
`ready()`で確認します。容量などを変更するときは`end()`の成功後、再設定してbeginします。
Hostからは切断・再列挙に見えます。Functionの容量・個数を指定する`build_opt.h`は不要です。

## 基本方針

- 必要なFunctionだけを選びます。全クラスをglobal driver表で参照せず、未使用クラスの実装や
  最大instance数分のbufferを一括で持ちません。
- 選択した資源を開始時に確保し、失敗時は戻します。通常の転送でheap確保を繰り返しません。
  対応クラスでは外部bufferやDMA領域の借用転送も選べます。
- descriptor、port、speed、endpoint packet size、raw reportを明示的に扱います。
  controllerのendpoint数・FIFO容量・帯域による物理上限は残ります。
- クラス、共通core、controller境界を本体で管理します。DWC2は
  [由来とライセンス](third_party/tinyusb/PROVENANCE.ja.md)を保持した保守forkです。
  vendored TinyUSB stack/configやSDK内のstackへのfallbackは持ちません。
- Arduino-ESP32/ESP-IDFのSoC・PHY・FreeRTOS・heap/cacheは利用します。
  Arduino標準USB Deviceの起動とは排他です。network・storage・firmware helperは任意とし、
  AudioへI2Sやcodecの実装を必須にしません。

HID Keyboard/Mouse/Gamepad/Consumer/System/Custom、CDC ACM、MIDI 1.0、Vendor bulk/control、
MSC、UAC1/UAC2 Audio、UVC Video、CDC-NCM、CCID、DFUを実装しています。
controllerのendpoint予算内で組み合わせられ、ライブラリ共通の4 Function制限はありません。
実装の有無と実機で確認した範囲は分けて扱います。下の「機種ごとの確認状況」を参照してください。

## ビルドと試験

開発の基準は**Arduino-ESP32 3.3.12**です。exampleの`sketch.yaml`もこの版に固定しています。
過去のCore互換表は当時のlibrary versionの記録です。v3の動作保証には使いません。

### 機種ごとの確認状況（2026-10-31）

| 機種 | 実機での確認 | 主な内容 |
| --- | --- | --- |
| ESP32-S3 | 済み（FS） | S3二台のpeer試験。列挙、各クラスの転送、所有権、reset/回復、故障注入 |
| ESP32-P4（旧P4 v1.3）とP4X（v3.2） | 済み（HS 480 Mbpsと FS） | 旧P4とP4XをHSで直結し、両方向でS3 peer試験39本とHS用の試験（PSRAMを保存先/ringに使う試験を含む）を実行。一台のloopback（FS）で両controller・全10クラス・Audio・借用DMA・PSRAM。P4XはLinux PCへのHS直結でDFU/MSCも確認 |
| ESP32-S2 | compileのみ | 実機は調達中 |
| ESP32-S31 | 未対応 | 機材は調達中。ビルドにArduino-ESP32 4.0.0-RC1が必要 |

まだ確認していない主なものは、S2/S31の実機、Arduino-ESP32 4系、Windows/macOSを含むPC OSとの互換性です。
残作業は[TODO](TODO.ja.md)、現在の状況は[引き渡し](docs/V3_HANDOFF.ja.md)にまとめています。

### 試験の実行

試験は独立した[validation環境](validation/README.ja.md)で、plugin標準の`--combo all`を使って実行します。
`--combo all`は、結線済みとして登録した組（S3二台、P4 loopback、P4/P4XのHS直結、P4XとPC）を順に実行します。
`--noconftest`を使い、旧2.x試験とは独立しています。
検証記録には対象commitと範囲を書いています。全OS・全control順序・全機種での確認を意味するものではありません。

[サンプル一覧](examples/README.md)に基本例、複合構成、同じbinaryでの容量変更を用意しています。
ビルド例:

```sh
arduino-cli compile --profile esp32s3 examples/CdcConsole
python3 tools/build_check.py esp32s3
python3 tools/verify_usb_backend.py
```

## v3 APIガイド

- [開始・停止、worker設定、移行](docs/V3_MIGRATION.ja.md)
- [Vendor・借用転送](docs/V3_VENDOR_API.ja.md)、[CDC](docs/V3_CDC_API.ja.md)
- [HID](docs/V3_HID_API.ja.md)、[MIDI](docs/V3_MIDI_API.ja.md)、[MSC](docs/V3_MSC_API.ja.md)
- [Audio](docs/V3_AUDIO_API.ja.md)、[Video](docs/V3_VIDEO_API.ja.md)
- [NCM](docs/V3_NCM_API.ja.md)、[CCID](docs/V3_CCID_API.ja.md)、[DFU](docs/V3_DFU_API.ja.md)
- [CH32RVとのAPI調整メモ](docs/V3_CH32RV_API_ALIGNMENT.ja.md)
- [API変更の判断](docs/V3_API_DECISIONS.ja.md)、[開発計画](docs/V3_DEVELOPMENT_PLAN.ja.md)

2.xのexamples/testsは[legacy/v2](legacy/v2/README.md)へ保管しています。
`V3_`以外の文書にも2.xの記述が残っています。過去版の参照資料として扱い、v3のAPIや合否基準には使いません。
