# EspUsbDevice Serial

> English: [README.md](README.md)

USB CDC ACM serial device を作り、PC や USB host とテキストを送受信する例です。

Arduino-ESP32 標準の `USBSerial` / `USBCDC` は使いません。`EspUsbDeviceCdcSerial` を
`EspUsbDevice` に登録し、USB CDC class descriptor と endpoint をこのライブラリ側で
構成します。

## ハードウェア

- USB device 対応の ESP32-S3 など Arduino-ESP32 board
- PC、EspUsbHost を動かす別 ESP32、またはテスト治具などの USB host
- ログ確認用の Serial monitor 接続

## 動作内容

- `EspUsbDevice` を full-speed CDC ACM serial device として起動します。
- USB CDC 側に 3 秒ごとに `tick=...` を送信します。
- USB CDC 側から受信した 1 文字を `echo: x` として返します。
- Host が設定した line coding と DTR / RTS の状態を通常の Serial monitor に出力します。

## 使い方

1. sketch を書き込み、通常の Serial monitor を開きます。
2. USB device port を PC または Host に接続します。
3. PC 側で追加された serial port を開きます。
4. 文字を送ると `echo: ...` が返ります。
5. Serial monitor 側には `CDC_LINE_CODING` と `CDC_LINE_STATE` が出ます。

## 主要 API

- `EspUsbDeviceCdcSerial UsbSerial(device)` は CDC ACM function を登録します。
- `UsbSerial.available()` は Host から受信済みの byte 数を返します。
- `UsbSerial.read()` は Host からの byte を読みます。
- `UsbSerial.write()` / `print()` / `printf()` は Host へ送信します。
- `UsbSerial.flush()` は送信を flush します。
- `UsbSerial.connected()` は DTR が立っている状態を接続済みとして返します。
- `UsbSerial.onLineCoding(callback)` は baud / stop bits / parity / data bits の変更を受け取ります。
- `UsbSerial.onLineState(callback)` は DTR / RTS の変更を受け取ります。
- `UsbSerial.onRx(callback)` は Host から受信があったときに呼ばれます。

## 注意

- USB CDC とログ用 Serial monitor は別の経路です。
- PC 側の serial port 名は OS によって異なります。
- baud rate は USB CDC の line coding として通知されますが、USB の実転送速度そのものではありません。
- 既存 Arduino USB class と同時に使う設計ではありません。

## 関連

- [Keyboard](../Keyboard/) - HID keyboard device
- [Mouse](../Mouse/) - HID mouse device
