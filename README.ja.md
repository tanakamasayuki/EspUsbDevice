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

## 対応 chip と class

| chip | 速度 | [HID](docs/classes/hid.ja.md) | [CDC](docs/classes/cdc.ja.md) | [Vendor](docs/classes/vendor.ja.md) | [MIDI](docs/classes/midi.ja.md) | [MSC](docs/classes/msc.ja.md) | [Audio](docs/classes/audio.ja.md) | [Video](docs/classes/video.ja.md) | [NCM](docs/classes/ncm.ja.md) | [CCID](docs/classes/ccid.ja.md) | [DFU](docs/classes/dfu.ja.md) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| ESP32-S3 | FS（12 Mbps） | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| ESP32-P4 v1.x と P4X（v3.1 以降） | HS（480 Mbps）と FS | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| ESP32-S2 | FS（12 Mbps） | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ |

✅ = `validation/` の試験で、実機の転送を確かめています。ESP32-S3 二台の FS、P4 と P4X を HS で
直結した両方向、P4 一台の HS と FS controller の結線、P4X と Linux PC です。
○ = ビルドできます。実機ではまだ動かしていません。ESP32-S31 にはまだ対応していません。

class：HID（NKRO 付き keyboard、mouse、gamepad、consumer control、system control、custom・vendor
report）、CDC ACM、vendor の bulk と control（WebUSB、WinUSB）、MIDI 1.0、MSC（RAM・FAT・SD card・
firmware の disk）、UAC1/UAC2 audio、UVC video、CDC-NCM の network、CCID の smart card reader、DFU 1.1。
自由に組み合わせて 1 つの複合 device にできます。上限は controller の endpoint と FIFO だけで、
収まらない構成は `begin()` が拒否します。

## 必要なもの

- Arduino-ESP32 **3.3.12**（例の `sketch.yaml` で固定しています）。
- ESP32-S3 と ESP32-S2：**ツール > USB Mode** を **USB-OTG (TinyUSB)**（`USBMode=default`）にします。
  ESP32-S3 の既定は Hardware CDC and JTAG です。
- ESP32-P4：**ツール > Chip Variant** で revision を選びます。v1.x は `prev3`、v3.1 以降（P4X）は `postv3` です。
- core 自身の USB CDC・MSC・DFU を起動時に有効にしたり、`USB.begin()` を呼んだりしないでください。
  USB OTG controller はこのライブラリが使います。

## 特長

- **明示的で、使う分だけ。** 容量、転送方式、buffer は instance ごとに、実行時に `configure()` で
  決めます。`build_opt.h` は要りません。`begin()` が確保し、`end()` が解放し、失敗したときは何も残しません。
- **所有権がはっきりしている。** コピー、callback、借用 DMA の転送。借りた buffer は、取消しや
  bus reset、`end()` のときも含めて、必ず一度だけ返ります。ring や保存先は PSRAM に置けます。
- **ESP32-P4 の HS。** 512 byte の bulk、1024 byte の interrupt、480 Mbps での isochronous の
  audio と video。`device.speed()` と TEST_MODE。
- **規格どおり。** descriptor、control 要求、class の動作は USB 2.0 と各 class の規格に従います。
  ライブラリが選んだところは、[class の文書](docs/classes/)に規格の箇所を書いています。
- **原因が分かる。** `begin()` は bus に触る前に endpoint と FIFO の予算を確かめ、
  `device.lastErrorName()` が理由を示します。

## 文書

| 文書 | 内容 |
| --- | --- |
| [USB device ガイド](docs/usb-device-guide.ja.md) | USB の基礎、電源と connector、ESP32 固有の注意、立ち上げ |
| [class の文書](docs/classes/) | class ごとの使い方、全設定、動作、Host OS 側の注意 |
| [詳細ガイド](docs/usb-device-advanced.ja.md) | task モデル、descriptor の詳細、control 転送、FIFO、DMA と PSRAM |
| [サンプル](examples/README.ja.md) | 40 本以上の完結したスケッチ。2.x の例もすべて含む |
| [トラブルシューティング](docs/troubleshooting.ja.md) | `begin()` のエラー、列挙の問題、OS ごと・class ごとの問題 |
| [USB 経由の OTA](docs/ota-over-usb.ja.md) | ファームウェア更新の経路：ROM loader、DFU、drive、serial、vendor、browser |
| [v2 からの移行](docs/migrating-from-v2.ja.md) | EspUsbDevice 2.x のスケッチを 3.0 へ |
| [Arduino-ESP32 USB からの移行](docs/migrating-from-arduino-esp32-usb.ja.md) | `USBHIDKeyboard`、`USBCDC`、`USBMSC` などから |
| [変更履歴](CHANGELOG.md) | 版ごとの変更 |

## まだ確かめていないこと

Windows と macOS の Host、ESP32-S2 の実機、実際の SD card と OTA の書込み、Arduino-ESP32 4.x。
試験の Host は ESP32 と Linux PC です。[試験](docs/development/testing.ja.md)を参照してください。

## ライセンス

MIT です。[LICENSE](LICENSE) を参照してください。DWC2 controller の driver は TinyUSB（MIT）と
ST のコード（BSD-3-Clause）に由来します。[third_party/tinyusb](third_party/tinyusb/PROVENANCE.ja.md) を参照してください。
