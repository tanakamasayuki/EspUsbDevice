# EspUsbDevice Composite (HID + CDC + MSC)

> English: [README.md](README.md)

HID keyboard・CDC ACM serial port・mass-storage FAT RAM disk を **1 台の USB device**
として同時に見せる複合（composite）デバイスの例です。

Arduino-ESP32 標準の USB class は使いません。3 つの Function を **1 つの `EspUsbDevice`** に
登録し、それぞれを設定してから `begin()` を 1 回呼ぶだけで、複合 USB descriptor と endpoint を
このライブラリ側で構成します。

```cpp
EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);
EspUsbDeviceCdcSerial console(device, "Composite console");
EspUsbDeviceMsc msc(device);

console.configure(serialConfig);   // 受信・送信ringの容量
msc.configure(mscConfig);          // 転送用stagingの大きさ
disk.attach(msc);                  // FAT RAM disk: geometryとcallback
device.begin(identity);            // 3つまとめて1回のbegin()
```

## ハードウェア

- USB device 対応の ESP32-S3 など Arduino-ESP32 board（ESP32-S2、ESP32-P4 も可）
- PC、EspUsbHost を動かす別 ESP32、またはテスト治具などの USB host
- ログ確認用の Serial monitor 接続

## 動作内容

- HID keyboard + CDC serial + MSC FAT RAM disk を 1 台の複合デバイスとして起動します。
- `README.TXT` を含む 48 KiB の removable drive を見せます。
- USB CDC で受信した 1 行を `echo: ...` として返します。
- CDC で `type <text>` の行を受け取ると、`<text>` を HID keyboard で入力します。
- terminal が port を開いている間、USB CDC 側に 5 秒ごとに `tick=...` を送信します。
- CDC の DTR/RTS の変化を debug 用 Serial に表示します。

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
  `EspUsbDeviceCdcSerial console(device, name)`、`EspUsbDeviceMsc msc(device)`。
- `device.begin()` の前に各 function を設定します。keyboard の layout、`console.configure()` での
  CDC の ring 容量、MSC の識別子、`msc.configure()` での staging の大きさ、FAT disk の中身です。
- `EspUsbDeviceMscFatRamDisk::format()` / `addTextFile()` で volume を作り、`attach(msc)` が
  geometry と callback を設定します。`mediaPresent(true)` と `isWritable(true)` は明示します。
- `device.begin(config)` を 1 回呼ぶと、全 function がまとめて enumerate されます。interface 番号と
  endpoint はライブラリが割り当て、複合 configuration descriptor を構成します。
- `console.onLineState()` は DTR/RTS を通知し、`console.connected()` は DTR が立っている間 true です。

## 注意

- これは ESP32-S2/S3 の非control IN endpoint上限4本に達する構成です
  （keyboard、CDC notification、CDC data-in、MSC bulk-in）。data endpointのIN/OUTは
  同じendpoint番号を共有します。MIDIやbulk Vendorを追加するとIN上限を超え、`device.begin()`が
  `ESP_ERR_INVALID_SIZE`で失敗します。ESP32-P4 の HS port は 7 本です。
  [USB Device開発ガイド（上級編）](../../docs/usb-device-advanced.ja.md) の controller の表（endpoint 番号と FIFO）を参照してください。
- MSC の `transferBytes` は bulk packet の倍数が必要です。512 なら full speed と high speed の両方で
  使えます。64 や 256 は P4 の HS port で失敗します。
- 48 KiB の disk は ESP32-S2 の static RAM に収まる大きさです。中身は reset で消えます。Host と
  スケッチが同時に filesystem を書き換えないようにします。
- USB CDC とログ用 Serial monitor は別の経路です。
- 既存 Arduino USB class と同時に使う設計ではありません。

## 関連

- [Keyboard](../Keyboard/) - HID keyboard device
- [CdcConsole](../CdcConsole/) - CDC ACM serial device
- [MscCdcBlocks](../MscCdcBlocks/) - MSC の raw RAM block と CDC
- [MSCFatRamDisk](../MSCFatRamDisk/) - v2 の FAT RAM disk の例

## 読み方と確認の手順

### 実行前の準備

boardに合うprofileを選びます（既定は`esp32s3`。ほかに`esp32s2`、v1.xのP4は`esp32p4`、
v3.1以上のP4は`esp32p4x`。v3.0は対象外）。診断Serialの接続先も確認します。
[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と
相手の利用可能状態の違いを説明しています。

### コードを追う

1つのdeviceが3つのfunctionを持ちます。CDC portを開き、試し書き用editorにfocusを置いて`type`
命令を使います。diskはfile managerで確認します。`line`は入力の長さを制限し、長い行は黙って
切り詰めます。長い命令を受け付けるアプリでは、この方針を見直してください。

### APIの使い方と改造時の注意

blockはLBAで指定し、大きさは媒体で決まります。fileを扱うにはその上にfilesystemが必要です。
容量、mountの成功、file操作を別々に確かめます。媒体を外したり再利用したりする前に、Hostの書込みの
完了を待ちます。USBの接続やFATの署名だけでは、filesystemが正しいことは保証されません。

媒体を書き換えるのは一度に一者だけにします。Hostがmountしたvolumeとスケッチが、同じfilesystemを
別々に編集しないでください。RAMの例は、reset で変更が消えます。

### 一つずつ確認する

最初は例を変更せずに動かし、Functionや容量は一つずつ変えます。例のエラー表示は、再試行や回復の
方針ではありません。

## v2からの変更点

- CDCのringとMSCのstagingは、build flagではなくFunctionごとの`configure()`で容量を決めます。
- `configure()`と`onLineState()`は`esp_err_t`を返し、スケッチで確認して理由を表示します。
  `attach()`が失敗したときは`msc.startError()`を表示します。
- FAT diskの`addTextFile()`の結果も確認します。
- CDCの`print()`／`printf()`と`flush()`は、空きと送信完了をTX timeout（既定1000 ms）まで待ちます。
  `console.setTxTimeoutMs(0)`にすると、入った分だけで直ちに戻ります。

Arduino-ESP32 3.3.12で`sketch.yaml`の全profileのcompileを確認しています。このスケッチ自体の
実機試験はしていません。

- [スケッチ](CompositeHidCdcMsc.ino)
- [全サンプル](../README.md)
- [HID](../../docs/classes/hid.ja.md)、[CDC](../../docs/classes/cdc.ja.md)、
  [MSC](../../docs/classes/msc.ja.md) API
- [サンプルの進め方](../../docs/example-workflow.ja.md)
