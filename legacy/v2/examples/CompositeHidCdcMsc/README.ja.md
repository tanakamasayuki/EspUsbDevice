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

- これは ESP32-S3 の非control IN endpoint上限4本に達する構成です
  （keyboard、CDC notification、CDC data-in、MSC bulk-in）。data endpointのIN/OUTは
  同じendpoint番号を共有します。MIDIやbulk Vendorを追加するとS3のIN上限を超えます。
  [../../docs/DESIGN_NOTES.ja.md](../../docs/DESIGN_NOTES.ja.md)「複合時の endpoint 予算の上限」
  参照。ESP32-P4 ではより多くの endpoint が使えます。
- USB Audioも同じcomposite descriptor builderを使います。UAC1がdefaultでUAC2は
  明示選択です。どちらもdescriptor容量とtargetのendpoint予算に制約されます。
- USB CDC とログ用 Serial monitor は別の経路です。
- 既存 Arduino USB class と同時に使う設計ではありません。

## 関連

- [Keyboard](../Keyboard/) - HID keyboard device
- [Serial](../Serial/) - CDC ACM serial device
- [MSCFatRamDisk](../MSCFatRamDisk/) - FAT RAM disk mass-storage device

## 読み方と確認の手順

### 実行前の準備

CDCの操作port・キーボード・FAT RAM diskを組み合わせる。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

一つのdeviceが3機能をまとめます。CDCを開き、試し書き用editorへフォーカスを合わせてtype命令を使い、file managerでは別のdiskを見ます。lineBufferで受信長を制限するため、実用途で長い命令を受ける前に長さ超過の扱いを確認します。

### APIの使い方と改造時の注意

blockはLBAと媒体のblock長で扱い、ファイルにはその上のfilesystemが必要です。容量取得、mount、ファイル操作の成否を分けます。返却byte数を確認し、handleを閉じ、ホストの未完了書き込みを終えてから取り外し・再利用します。USB接続やFAT署名だけではfilesystemの正常を保証しません。

同じ媒体の書き手は一つにします。ホストがmountしたvolumeを、スケッチも独立に書き換えないようにします。RAM例はresetで変化が失われますが、実媒体の例は永続データを変えます。残したいファイルがある媒体を使う前に、どの操作を行う例かを確認します。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](CompositeHidCdcMsc.ino)
- [全サンプル](../README.ja.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
