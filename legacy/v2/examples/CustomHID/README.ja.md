# EspUsbDevice CustomHID

> English: [README.md](README.md)

任意の HID report descriptor を渡して、custom HID device を作る例です。

この example は vendor-defined usage page `0xff00` の 8 byte Input report を 1 秒ごとに
Host へ送信します。Keyboard、Mouse、Gamepad、VendorHID の定型 class に当てはまらない
小さな HID device を試すための出発点です。

## ハードウェア

- USB device 対応の ESP32-S3 など Arduino-ESP32 board
- EspUsbHost、HID parser、独自 Host application などの USB host
- ログ確認用の Serial monitor 接続

## 動作内容

- `REPORT_DESCRIPTOR` を sketch 内で定義します。
- `EspUsbDeviceHidCustom` に descriptor と input report size を渡します。
- 1 秒ごとに 8 byte report を Host へ送信します。
- Serial monitor に送信 counter を出力します。

## 主要 API

- `EspUsbDeviceHidCustom customHid(device, descriptor, descriptorLength, inputReportSize)` は custom HID function を登録します。
- `customHid.sendReport(data, length)` は report ID なしの Input report を送信します。
- `customHid.sendReport(data, length, reportId)` は report ID 付き descriptor 用に使います。

## Descriptor

この example の descriptor は次の report を定義します。

- usage page: vendor-defined `0xff00`
- report ID: なし
- input report size: 8 bytes
- report count: 8
- report size: 8 bits

descriptor と `inputReportSize` が一致していないと、Host 側 parser や endpoint MPS の期待と
実際の report 長がずれます。まず小さい固定長 report から始めるのがおすすめです。

## VendorHID との違い

- `VendorHID` は定型の vendor-defined report descriptor と Output / Feature callback を持ちます。
- `CustomHID` は descriptor を sketch 側で完全に定義します。
- 独自 descriptor を検証したい場合は `CustomHID`、単純な独自バイナリ通信なら `VendorHID` が向いています。

## 関連

- [VendorHID](../VendorHID/) - 定型 vendor-defined HID
- [Gamepad](../Gamepad/) - HID gamepad device

## 読み方と確認の手順

### 実行前の準備

独自の入力専用HID reportを定義して送る。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

`REPORT_DESCRIPTORとcustomHid.sendReport()`のbyte列を並べて読みます。ホスト側のreport ID・field数・長さを合わせます。この例だけでOutput・Featureの手順も生まれるわけではありません。descriptor配列はdeviceの使用中ずっと残します。

### APIの使い方と改造時の注意

HID reportは構造化された状態や変化であり、任意のテキスト一行ではありません。report descriptorと合わせて、report ID・usage page/usage・符号・数値範囲を読みます。キーボードはキー位置、マウス移動は相対値、ボタンはbitmapです。実際の操作へつなぐ前に、押下と解放の対、または全状態の比較を確認します。

callbackのreportは借りたデータです。別taskへ渡す場合はbyte列や必要な値を自分の領域へコピーします。callbackでは短い診断に留め、生dumpの連続表示で端末速度が律速になることにも注意します。入力が来なければ、列挙、descriptor・report長、アプリ側の解釈の順に調べ、一度に全部を変えないようにします。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](CustomHID.ino)
- [全サンプル](../README.ja.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
