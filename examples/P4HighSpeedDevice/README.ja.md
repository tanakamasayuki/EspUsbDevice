# ESP32-P4 High-Speed Device

> English: [README.md](README.md)

HID keyboardをESP32-P4のHighSpeed Device controllerで動かします。

- Device controller: `EspUsbController::HighSpeed`（P4のHS port。endpoint 0〜15、非controlの
  IN endpointは7本、480 Mbps）
- PHY: UTMI High-Speed PHY
- 接続先: board上でUTMI PHYへ配線されたUSB connector

```cpp
EspUsbDeviceConfig config;
config.controller = EspUsbController::HighSpeed;  // P4ではAutoでもこれを選ぶ
config.vbusSensing = false;                       // 下の「VBUS」を参照
device.begin(config);
```

connector名、VBUS switch、USB-C CC処理はboardごとに異なるため、回路図を確認してください。
USB Serial/JTAG connectorやGPIO26/GPIO27のFS pairはHigh-Speed Device portではありません。

列挙後に1行だけkeyboard入力し、`device.speed()`で決まった速度（`EspUsbBusSpeed::High`か`Full`）を表示します。
FSのHostやhubへ接続した場合、このcontrollerを使っていてもlink自体はFull Speedになり、
`device.speed()`は`Full`を返します。

## High Speedで変わること

High Speedではbulkの最大packetが64から512 byteに、interrupt endpointは1024 byteまでになります。
Full Speedで開始できた設定でも、このcontrollerでは`begin()`が失敗することがあるので、`begin()`と
`device.lastErrorName()`を確認します。

| Function | High Speedでの条件 |
| --- | --- |
| MSCの`transferBytes` | 512の倍数（64や256は`ESP_ERR_INVALID_SIZE`） |
| CCIDの`messageBytes` | 自動（0）なら512。512未満の明示値は失敗 |
| 借用受信（`readDirect`）の容量 | 512の倍数 |
| CDC／Vendor／MIDIの`transferBytes` | 512未満でも受け付ける |
| 送信FIFO（4 KiBを共有） | `begin()`がすべてのIN endpointを検査する。512 byteのbulk Vendorは5個まで入り、6個目は`ESP_ERR_INVALID_SIZE` |

## VBUS

`config.vbusSensing = false`（既定）では、controllerはVBUSがあるものとして動きます。検出回路のない
boardに向いた設定です。`true`にするのは、boardがVBUS検出信号をこのcontrollerへ配線している場合
だけです。このoptionはGPIOや回路を追加しません。v3のbench試験では、v1.xのP4のこのportで
session-validのbitがVBUSの有無を示さず、また一方のboardのHS portからもう一方へ電源が回り込みました。
board同士をport直結する前に、回路図を確認してください。

## 読み方と確認の手順

### 実行前の準備

chip revision v1.xのP4は`esp32p4`（ChipVariant=prev3）、v3.1以上は`esp32p4x`（ChipVariant=postv3）のprofileを選びます（v3.0は対象外）（どちらも
OTG controllerを使う`USBMode=default`）。診断Serialの接続先を確認します。
[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と
相手の利用可能状態の違いを説明しています。

### コードを追う

`begin()`前の`config.controller`でHighSpeedを選びます。`device.ready()`後に一度挨拶を送るので
試し書き用editorを使います。配線されたコネクタ・PHYと選択を合わせます。modeの指定だけでは
基板配線・VBUS切替・USB-C CC回路は変更されません。

### APIの使い方と改造時の注意

選択controller、PHY・pad、基板コネクタ、給電回路を区別します。controller optionが基板配線まで
変えるわけではありません。例のtarget・profileを使い、VBUS・USB-Cの役割回路は基板の回路図に
従います。一controller・一相手で動かしてから、別portのHostとDeviceを組み合わせます。

### 一つずつ確認する

最初は例を変更せずに動かし、ログとHostの状態を照合します。相手がいない場合から接続へ進みます。
例のエラー表示は、再試行や回復の方針ではありません。

## v2からの変更点

- TinyUSBのport番号（rhport 1）は関係しなくなり、controllerは`EspUsbDeviceConfig::controller`
  だけで選びます。
- `config.vbusSensing`を明示しました。旧build flagの`CFG_TUD_VBUS_DETECT_HW`の代わりで、旧flagは
  compile errorになります。
- profileはP4の両世代を扱います。`esp32p4`（ChipVariant=prev3）と`esp32p4x`（ChipVariant=postv3）
  です。
- v3が`begin()`で確かめるHigh Speedでの大きさの条件をREADMEに載せました。
- 新しい`device.speed()`で、決まった速度を表示します。

Arduino-ESP32 3.3.12で両profileのcompileを確認しています。このスケッチ自体の実機試験はしていません。

- [スケッチ](P4HighSpeedDevice.ino)
- [P4FullSpeedDevice](../P4FullSpeedDevice/)
- [全サンプル](../README.md)
- [USB Device開発ガイド](../../docs/usb-device-guide.ja.md)（ESP32固有の注意：P4でのHS／FSの選択、HSで変わること、旧P4とP4X）
- [サンプルの進め方](../../docs/example-workflow.ja.md)
