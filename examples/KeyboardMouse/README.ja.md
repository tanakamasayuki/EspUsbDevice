# EspUsbDevice KeyboardMouse

> English: [README.md](README.md)

1台の ESP32 から keyboard report と mouse report を出す composite USB HID device の例です。

## ハードウェア

- USB device 対応の ESP32-S3 など Arduino-ESP32 board
- PC、EspUsbHost を動かす別 ESP32、またはテスト治具などの USB host
- ログ確認用の Serial monitor 接続

## 動作内容

- `EspUsbDeviceHidKeyboard` と `EspUsbDeviceHidMouse` を同じ `EspUsbDevice` に登録します。
- layout 対応 keyboard wrapper で文字を送信します。
- mouse movement と left click を送信します。
- このライブラリの composite HID descriptor 構成を確認できます。

## Composite HID の形

現時点の composite HID は、単一 HID interface と report ID で構成します。
これは、このライブラリが使う Arduino-ESP32 TinyUSB runtime で安定して列挙するための構成です。

| Report | Report ID |
|--------|-----------|
| Keyboard | `1` |
| Mouse | `2` |

report ID 付き keyboard report は 9 bytes になるため、composite HID interrupt endpoint
MPS は `16 bytes` です。

## 主要 API

- 複数 class object を同じ `EspUsbDevice` instance で作ります。
  `EspUsbDeviceHidKeyboard keyboard(device);`
  `EspUsbDeviceHidMouse mouse(device);`
- すべての class object を作った後、`device.begin(config)` を1回呼びます。
- keyboard API は [Keyboard](../Keyboard/) を参照してください。
- mouse API は [Mouse](../Mouse/) を参照してください。

## 想定 Serial 出力

```text
USB keyboard + mouse ready
KEY_FAILED error=ESP_ERR_TIMEOUT
MOVE_FAILED error=ESP_ERR_TIMEOUT
CLICK_FAILED error=ESP_ERR_TIMEOUT
```

起動後、この example は失敗時だけ Serial に出力します。keyboard と mouse の挙動は USB host 側で確認してください。

## 関連

- [Keyboard](../Keyboard/) - HID keyboard device
- [Mouse](../Mouse/) - HID mouse device

## 読み方と確認の手順

### 実行前の準備

一つのHID機器でキーボードとマウスのreportを組み合わせる。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

両機能を同じdeviceへbegin()前に登録します。文字と移動が自動生成されるため試し書き用文書で試し、report IDをdescriptorと比べます。機能追加でreport配置も変わるので、単機能の生HID clientの前提を流用しないようにします。

### APIの使い方と改造時の注意

HID reportは構造化された状態や変化であり、任意のテキスト一行ではありません。report descriptorと合わせて、report ID・usage page/usage・符号・数値範囲を読みます。キーボードはキー位置、マウス移動は相対値、ボタンはbitmapです。実際の操作へつなぐ前に、押下と解放の対、または全状態の比較を確認します。

callbackのreportは借りたデータです。別taskへ渡す場合はbyte列や必要な値を自分の領域へコピーします。callbackでは短い診断に留め、生dumpの連続表示で端末速度が律速になることにも注意します。入力が来なければ、列挙、descriptor・report長、アプリ側の解釈の順に調べ、一度に全部を変えないようにします。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](KeyboardMouse.ino)
- [全サンプル](../README.ja.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
