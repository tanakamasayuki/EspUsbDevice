# EspUsbDevice VendorEcho

> English: [README.md](README.md)

最小の vendor 固有 bulk device です。Host が bulk OUT に書いた byte を、そのまま bulk IN で返します。

この例の要点は送信の繰り返しです。`EspUsbDeviceVendor::write()` は待ちません。送信 ring に入る分だけ写し、その byte 数を返します。この例は書ききれなかった残りを保持し、次の `loop()` で送り直します。Host の読み出しが遅くても data を捨てません。

## ハードウェア

- native USB OTG port を持つ ESP32-S2 / S3 / P4 board
- vendor interface を開ける USB host（libusb / pyusb / WinUSB を使う PC、または EspUsbHost を動かす別 ESP32）
- Serial monitor への出力はありません

## 動作内容

- vendor function の受信 ring と送信 ring を 256 byte、`transferBytes` を 64 にして、既定の identity（VID `0x303a`、PID `0x4000`）で `EspUsbDevice` を開始します。
- Host が構成している間、最大 64 byte を `pending` に読み、送り返します。全部受け付けられるまで `length` / `sent` を保ちます。
- Host がいなくなる（`device.ready()` が false になる）と、保持中の chunk を捨てます。

## Host 側での確かめ方

device には `bInterfaceClass = 0xff` の interface が 1 つあり、bulk OUT と bulk IN が 1 組あります。endpoint address は決め打ちせず、descriptor から読みます。

```python
# pyusb（libusb backend）
import usb.core, usb.util
dev = usb.core.find(idVendor=0x303a, idProduct=0x4000)
intf = dev.get_active_configuration()[(0, 0)]
def ep(direction):
    return usb.util.find_descriptor(intf, custom_match=lambda e:
        usb.util.endpoint_direction(e.bEndpointAddress) == direction)
ep(usb.util.ENDPOINT_OUT).write(b"hello vendor")
print(bytes(ep(usb.util.ENDPOINT_IN).read(512, timeout=1000)))
```

- Linux：kernel driver は付きません。root で動かすか、VID/PID の udev rule を足します。
- Windows：ライブラリの Microsoft OS 2.0 descriptor により、`.inf` なしで WinUSB が付きます。この例では Windows で未確認です。
- macOS：libusb で開けます。未確認です。
- 読む長さは bulk packet の大きさ（FS は 64、HS は 512）の倍数にします（上の script と同じ）。

## 主要 API

- `EspUsbDeviceVendor vendor(device)` は vendor function を登録します。省略できる第 2 引数は FS の bulk packet 長で、8 / 16 / 32 / 64 です（USB 2.0 §5.8.3）。ほかの値では `device.begin()` が `ESP_ERR_INVALID_SIZE` で失敗します。HS は常に 512 です。
- `vendor.configure(EspUsbVendorConfig)` で `rxCapacity`、`txCapacity`、`transferBytes`、受信 mode を決めます。停止中だけ受け付けます。
- `vendor.read(buffer, size)` は受信済みの byte を写します。待ちません。
- `vendor.write(buffer, size)` は送信 ring に入る分だけ写し、その数を返します。待ちません。Host が構成していない間は 0 を返します。
- `device.ready()` は Host が構成している間 true です。

## 注意

- `transferBytes = 64` は IN 送信 1 回の上限です。P4 の HS controller（bulk packet 512）では毎回 short packet になります。速度が要るときは 512 以上にします。
- bulk は byte の流れです。Host の複数回の write が 1 回の `read()` に届くことも、1 回の write が分かれて届くこともあります。アプリの message には長さを付けます。
- Vendor の既定では、packet 境界で終わる送信の後に ZLP を付けません。Host 側で short packet を待たないでください。
- Host が読む前に受信 ring と送信 ring の合計（ここでは約 512 byte）より多く書くと、両方が待ち続けます。Host 側では読み書きを並行させます。
- `configure()` や `begin()` が失敗すると、別の debug 接続の `Serial`（115200）にエラー名を表示します。

## 関連

- [USBVendor](../USBVendor/) - vendor request、WebUSB、ログ付きの echo
- [ReconfigureVendor](../ReconfigureVendor/) - 同じ echo で ring の大きさを実行中に変える
- [MidiEcho](../MidiEcho/) - MIDI packet で同じ「保持して送り直す」形
- [Vendor API](../../docs/classes/vendor.ja.md)

## 読み方と確認の手順

### 実行前の準備

上のハードウェアの条件を確認し、board と例の profile を選びます。[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USB の役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

`loop()` が持つのは最大 64 byte の chunk 1 つです。前の chunk を全部受け付けてもらうまで次を読みません。Host が読むのを止めると受信 ring が埋まり、device は OUT data を受け取らなくなります。これが backpressure の経路です。

### API の使い方と改造時の注意

USB の IN/OUT は Host から見た向きです。IN は Host へ、OUT は Host から。`write()` が成功しても、写しただけで Host が読んだとは限りません。chunk や ring の大きさを変えるときも「残りを保持する」処理は残します。

### 一つずつ確認する

最初は例を変えずに動かし、送った内容と返った内容を比べます。次に値（ring の大きさ、`transferBytes`、chunk の大きさ）を一つだけ変えて繰り返します。

## v2 からの変更点

この例は v3 で新しく作りました。v2 には対応する例がありません。

- [スケッチ](VendorEcho.ino)
- [全サンプル](../README.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
