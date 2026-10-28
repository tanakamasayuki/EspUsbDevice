# ESP32-P4 High-Speed Device

> English: [README.md](README.md)

HID keyboardをESP32-P4のHighSpeed Device controllerで動かします。

- Device controller: `EspUsbController::HighSpeed`
- TinyUSB port: rhport 1
- PHY: external UTMI High-Speed PHY
- 接続先: board上でUTMI PHYへ配線されたUSB connector

connector名、VBUS switch、USB-C CC処理はboardごとに異なるため、回路図を確認してください。
USB Serial/JTAG connectorやGPIO26/GPIO27のFS pairはHigh-Speed Device portではありません。

列挙後に1行だけkeyboard入力します。FS Hostへ接続した場合、このcontrollerを使っていても
link自体はFull Speedでネゴします。

## 読み方と確認の手順

### 実行前の準備

P4のHighSpeed controllerでUSBキーボードを開始する。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

`begin()`前のconfig.controllerでHighSpeedを選びます。`device.ready()`後に一度挨拶を送るので試し書き用editorを使います。配線されたコネクタ・PHYと選択を合わせます。modeの指定だけでは基板配線・VBUS切替・USB-C CC回路は変更されません。

### APIの使い方と改造時の注意

選択controller、PHY・pad、基板コネクタ、給電回路を区別します。controller optionが基板配線まで変えるわけではありません。Serial/JTAGを動かす試験の前に書き込み・debug経路を確保し、必要なら独立UARTでログを見ます。

例のtarget・profileを使い、VBUS・USB-Cの役割回路は基板の回路図に従います。実行時のPHY選択とチップの永続設定は異なります。一controller・一相手で動かしてから、別portのHostとDeviceを組み合わせます。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](P4HighSpeedDevice.ino)
- [全サンプル](../README.ja.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
