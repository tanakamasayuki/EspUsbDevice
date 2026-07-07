# EspUsbDevice Composite (HID + CDC + MSC)

> English: [README.md](README.md)

HID keyboard・CDC ACM serial port・mass-storage FAT RAM disk を **1 台の USB device**
として同時に見せる複合（composite）デバイスの例です。

Arduino-ESP32 標準の USB class は使いません。3 つの `EspUsbDevice` function を
**1 つの `EspUsbDevice`** に登録し、`begin()` を 1 回呼ぶだけで、複合 USB descriptor と
endpoint をこのライブラリ側で構成します。

## ハードウェア

- USB device 対応の ESP32-S3 など Arduino-ESP32 board
- PC、EspUsbHost を動かす別 ESP32、またはテスト治具などの USB host
- ログ確認用の Serial monitor 接続

## 動作内容

- HID keyboard + CDC serial + MSC FAT RAM disk を 1 台の複合デバイスとして起動します。
- `README.TXT` を含む removable drive を見せます。
- USB CDC で受信した 1 行を `echo: ...` として返します。
- CDC で `type <text>` の行を受け取ると、`<text>` を HID keyboard で入力します。
- USB CDC 側に 5 秒ごとに `tick=...` を送信します。

## 使い方

1. sketch を書き込み、通常の Serial monitor を開きます。
2. USB device port を PC または Host に接続します。
3. removable drive が現れるので `README.TXT` を開きます。
4. PC 側で追加された serial port を開きます。
5. `type Hello` と送ると、フォーカスのあるウィンドウに `Hello` が入力されます。
6. それ以外の行を送ると `echo: ...` が返ります。

## 主要 API

- 各 function を **同じ** `EspUsbDevice` に登録します:
  `EspUsbDeviceHidKeyboard keyboard(device)`、
  `EspUsbDeviceCdcSerial UsbSerial(device)`、`EspUsbDeviceMsc msc(device)`。
- `device.begin()` の前に、各 function（layout、MSC の識別子、FAT disk の中身）を設定します。
- `device.begin(config)` を 1 回呼ぶと、全 function がまとめて enumerate されます。
- interface 番号と endpoint はライブラリが自動で割り当て、複合 configuration descriptor を構成します。

## 注意

- これは ESP32-S3 の USB endpoint 予算に収まる最大の複合構成です（FIFO を消費する IN
  endpoint が keyboard・CDC data-in・MSC bulk-in の 3 本）。4 本目の FIFO-IN class
  （例: MIDI や bulk Vendor）を足すと S3 の FIFO 予算を超え、enumerate に失敗します。
  [../../docs/DESIGN_NOTES.ja.md](../../docs/DESIGN_NOTES.ja.md)「複合時の endpoint 予算の上限」
  参照。ESP32-P4 ではより多くの endpoint が使えます。
- USB Audio class（`EspUsbDeviceAudio`）は排他で、他 class と複合できません。
- USB CDC とログ用 Serial monitor は別の経路です。
- 既存 Arduino USB class と同時に使う設計ではありません。

## 関連

- [Keyboard](../Keyboard/) - HID keyboard device
- [Serial](../Serial/) - CDC ACM serial device
- [MSCFatRamDisk](../MSCFatRamDisk/) - FAT RAM disk mass-storage device
