# ESP32-P4 Full-Speed Device

> English: [README.md](README.md)

HID keyboardをESP32-P4の内蔵FullSpeed Device controllerで動かします。

- Device controller: `EspUsbController::FullSpeed`（P4のFS port。endpoint 0〜6、非controlのIN
  endpointは4本）
- PHY: 内蔵のFull-Speed PHY
- USB OTG FS既定pin: GPIO26=D-, GPIO27=D+
- swap後のpin: GPIO24=D-, GPIO25=D+

```cpp
EspUsbDeviceConfig config;
config.controller = EspUsbController::FullSpeed;  // P4ではAutoならHSを選ぶ
config.vbusSensing = false;                       // 下の「VBUS」を参照
device.begin(config);
```

P4では既定の`EspUsbController::Auto`がHigh-Speed controllerを選ぶので、この例はFull-Speedの
controllerを明示します。指定したcontrollerがなければ`begin()`は`ESP_ERR_NOT_SUPPORTED`で失敗します。

## pinの切替え（任意）

sketchには任意のrouting callをコメントアウトした状態で記載しています。

```cpp
// usb_wrap_ll_phy_select(&USB_WRAP, 0);
```

`device.begin()`より前でコメントを外すとUSB OTG FSがGPIO24/GPIO25へ移り、USB Serial/JTAGは
GPIO26/GPIO27へ移ります。そのため、GPIO24/GPIO25で使用中のSerial/JTAG接続は切断されることが
あります。試す前に、別の書込み・debug経路（UART bridgeやBOOT button）を確保してください。

これはESP-IDFのHAL（`hal/usb_wrap_ll.h`）の呼出しで、EspUsbDeviceのAPIではありません。切り替えるのは
D-/D+だけです。VBUS給電・検出やUSB-C CC回路は設定しないため、配線・給電前にboardの回路図を
確認してください。

## VBUS

`config.vbusSensing = false`（既定）では、controllerはVBUSがあるものとして動きます。検出回路のない
boardに向いた設定です。`true`にするのは、boardがVBUS検出信号をこのcontrollerへ配線している場合
だけです。このoptionはGPIOや回路を追加しません。portへの給電はboardの回路図に従ってください。
USB-Cのconnectorでは、HostからVBUSを受けるのにCC抵抗が必要です。

## 動作

列挙後に`Hello from the ESP32-P4 FS device`を1行だけ、Host側でfocusのある場所へkeyboard入力
します。試し書き用のeditorを使ってください。

## 読み方と確認の手順

### 実行前の準備

chip revision v1.xのP4は`esp32p4`（ChipVariant=prev3）、v3.1以上は`esp32p4x`（ChipVariant=postv3）のprofileを選びます（v3.0は対象外）（どちらも
OTG controllerを使う`USBMode=default`）。診断Serialの接続先を確認します。多くのP4 boardでは
USB Serial/JTAGのportで、上のpin切替えで移動します。[サンプルの進め方](../../docs/example-workflow.ja.md)
では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

`begin()`前の`config.controller`でFullSpeedを選びます。`device.ready()`後に一度挨拶を送ります。
配線されたコネクタ・PHYと選択を合わせます。modeの指定だけでは基板配線・VBUS切替・USB-C CC回路は
変更されません。

### APIの使い方と改造時の注意

選択controller、PHY・pad、基板コネクタ、給電回路を区別します。controller optionが基板配線まで
変えるわけではありません。例のtarget・profileを使い、VBUS・USB-Cの役割回路は基板の回路図に
従います。実行時のPHY選択とチップの永続設定は異なります。一controller・一相手で動かしてから、
別portのHostとDeviceを組み合わせます。

### 一つずつ確認する

最初は例を変更せずに動かし、ログとHostの状態を照合します。相手がいない場合から接続へ進みます。
pinや設定は一つずつ変えます。例のエラー表示は、再試行や回復の方針ではありません。

## v2からの変更点

- TinyUSBのport番号（rhport 0）は関係しなくなり、controllerは`EspUsbDeviceConfig::controller`
  だけで選びます。
- `config.vbusSensing`を明示しました。旧build flagの`CFG_TUD_VBUS_DETECT_HW`の代わりで、旧flagは
  compile errorになります。
- profileはP4の両世代を扱います。`esp32p4`（ChipVariant=prev3）と`esp32p4x`（ChipVariant=postv3）
  です。
- sketch内のpin切替えのコメントを直しました（v2のsketchではその行が途中で切れていました）。

Arduino-ESP32 3.3.12で両profileのcompileを、切替えのcallをコメントアウトした状態と有効にした状態の
両方で確認しています。このスケッチも、v3でのpin切替えも、実機試験はしていません。

- [スケッチ](P4FullSpeedDevice.ino)
- [P4HighSpeedDevice](../P4HighSpeedDevice/)
- [全サンプル](../README.md)
- [USB Device開発ガイド](../../docs/usb-device-guide.ja.md)（ESP32固有の注意：P4でのHS／FSの選択、旧P4とP4X）
- [サンプルの進め方](../../docs/example-workflow.ja.md)
