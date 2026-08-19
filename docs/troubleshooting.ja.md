# トラブルシューティング

> English: [troubleshooting.md](troubleshooting.md)

EspUsbDeviceで起きる問題を「症状から引ける」形で1か所に集めたものです。
基礎と立ち上げ手順の全体は [USB Device開発ガイド](usb-device-guide.ja.md)、
ライブラリ内部は [応用ガイド](usb-device-advanced.ja.md) を参照してください。

ホストを繋ぐ前に、次の2つのスケッチでほとんどの疑問に答えが出ます。

| ツール | 分かること |
|--------|-----------|
| [`EspUsbDeviceBringUpCheck`](../examples/Info/EspUsbDeviceBringUpCheck/) | `begin()`が成功したか、ホストが列挙したか、どの速度か |
| [`EspUsbDeviceDescriptorDump`](../examples/Info/EspUsbDeviceDescriptorDump/) | ライブラリが組んだ全ディスクリプタとendpoint予算。ホスト不要 |

## 目次

1. [begin()が失敗する](#1-beginが失敗する)
2. [begin()は成功するがホストに何も出ない](#2-beginは成功するがホストに何も出ない)
3. [列挙は始まるが途中で失敗する](#3-列挙は始まるが途中で失敗する)
4. [ビルドと書き込み](#4-ビルドと書き込み)
5. [ホストOS別の観測](#5-ホストos別の観測)
6. [クラス別の問題](#6-クラス別の問題)
7. [安定性](#7-安定性)
8. [チップ別の注意](#8-チップ別の注意)
9. [それでも解決しないとき](#9-それでも解決しないとき)

## 1. begin()が失敗する

### `ESP_ERR_INVALID_SIZE`

構成がcontrollerのendpoint予算またはディスクリプタのサイズ上限を超えています。
チェックはPHY開始前に走るので、ホストには何も見えません。

1. 同じクラス構成（`DUMP_ENABLE_*` define）で
   [`EspUsbDeviceDescriptorDump`](../examples/Info/EspUsbDeviceDescriptorDump/)
   をビルドし、末尾のendpoint予算を読みます。ホスト不要です。
2. クラスを減らすか入れ替えます。ESP32-S2/S3の実質上限は**非control IN 4本**で、
   HID + CDC + MSC（1+2+1）で既に埋まります
   （[ガイド3.3](usb-device-guide.ja.md#33-endpoint予算)）。
3. ESP32-P4ならHS controllerを選びます。予算が大幅に広がります
   （[ガイド3.2](usb-device-guide.ja.md#32-esp32-p4のfshs選択)）。
4. endpoint予算に収まっている場合は、configuration descriptorが704バイト以内、
   各HID report descriptorが256バイト以内かを確認します
   （[ガイド3.4](usb-device-guide.ja.md#34-その他のライブラリ側の上限)）。

### `ESP_ERR_NOT_SUPPORTED`

1. ターゲットがESP32-S2 / S3 / P4かを確認します。無印ESP32、C3、C6などは
   USB-OTG device controllerを持たず、このライブラリは動きません
   （[ガイド3.1](usb-device-guide.ja.md#31-対応チップとcontroller)）。

### その他の`begin()`失敗

1. `device.lastErrorName()`を出力します。すべての失敗経路が理由を名乗ります。
2. Core Debug Levelを`Verbose`に上げてESP-IDFログを読みます
   （[ガイド3.6](usb-device-guide.ja.md#36-ログ)）。

## 2. begin()は成功するがホストに何も出ない

上から順に確認してください。どれもよくある原因で、前半2つはコードと無関係です。

1. **コネクタ違い。** 多くのボードにはOTG controllerに繋がっていないUARTや
   USB-Serial/JTAGのコネクタもあります。回路図を確認します
   （[ガイド1.2](usb-device-guide.ja.md#12-どのコネクタがデバイス側か)）。
2. **充電専用ケーブル。** データ通信できると分かっているケーブルに替えます。
3. **`USB Mode`が"Hardware CDC and JTAG"でビルドされている。** D+/D-が
   Serial/JTAGペリフェラルへ配線され、OTG controllerからバスが見えません。
   "USB-OTG (TinyUSB)"でビルドします
   （[ガイド3.5](usb-device-guide.ja.md#35-arduino-esp32標準usbスタックとは排他)）。
4. **ESP32-P4のみ: controllerとコネクタの不一致。** P4では`Auto`はHighSpeedに
   解決されます。FSピン（GPIO26/27）に繋いだ場合は
   `config.controller = EspUsbController::FullSpeed`を、ボードのHSコネクタなら
   HighSpeedを指定します
   （[ガイド3.2](usb-device-guide.ja.md#32-esp32-p4のfshs選択)）。
5. その上でBringUpCheckを実行し、[ホスト側のログ](#5-ホストos別の観測)を
   読みます。ここから先の原因はホスト側からしか見えません。

## 3. 列挙は始まるが途中で失敗する

### Windowsに「Unknown USB Device (Device Descriptor Request Failed)」と出る

ディスクリプタ段階で失敗しています。

1. [USB Device Tree Viewer](https://www.uwe-sieber.de/usbtreeview_e.html)で
   生のディスクリプタバイト列とWindowsの正確なエラーを読みます。
2. DescriptorDumpの出力と突き合わせます。Linuxなら
   [`tests/manual/device_inspect`](../tests/manual/device_inspect/)でも
   照合できます（`--json`でdiffが楽になります）。

### 変更したのにホストで古いデバイスのまま見える

**WindowsはVID/PIDごとにディスクリプタをキャッシュします。** デバイス開発で
「変更が反映されない」の原因第1位です。

1. 開発中にディスクリプタを変えるたびPIDを変えます（このリポジトリのInfo系
   スケッチがPIDを分けているのはこのためです）。
2. または`config.serialNumber`を変えます。
3. またはデバイスマネージャーでデバイスを削除して挿し直します。

### `SET_CONFIGURATION`まで進むがドライバがバインドしない

ディスクリプタの構造は正しいものの、class / subclass / protocolの組み合わせが
ホストOSのドライバ対応表に載っていない状態です。

1. 同じクラスの市販デバイスを並べてディスクリプタをdiffします
   （[ガイド5.5](usb-device-guide.ja.md#55-うまくいっている機器と比べる)）。
   インターフェースの並び、IADの有無、`bInterfaceProtocol`、エンドポイントの
   intervalが定番の差分です。

### 登録したはずのクラスがディスクリプタに現れない

1. 登録できるのは4クラスまでで、5個目の`addClass()`は**黙って**失敗します。
   DescriptorDumpでインターフェース一覧を確認します
   （[ガイド3.4](usb-device-guide.ja.md#34-その他のライブラリ側の上限)）。

## 4. ビルドと書き込み

1. **arduino-esp32 core 3.3.9以降を使います。** バージョン別のビルド結果は
   このディレクトリの[`COMPATIBILITY.<version>.md`](.)として公開しています。
2. **`USB.begin()`を呼ばない**でください。`USBHIDKeyboard`、`USBHIDMouse`、
   `USBCDC`との併用もできません。2つのスタックが1つのcontrollerを取り合います
   （[ガイド3.5](usb-device-guide.ja.md#35-arduino-esp32標準usbスタックとは排他)）。
3. **`USB CDC On Boot`は無効のまま**にします。有効だとcoreが自前のCDCを
   もう1本立ち上げようとします。
4. この構成では`Serial`はUART側に出ます。ボードのコネクタがOTGの1つだけの
   場合、ログとデバイス役が同じ差し込み口を取り合うので、開発中は2コネクタの
   ボードかUARTアダプタを使います
   （[ガイド2.3](usb-device-guide.ja.md#23-開発中のコネクタ構成)）。

## 5. ホストOS別の観測

観測方法の全体は[ガイド5章](usb-device-guide.ja.md#5-ホストosから自分を観測する)
にあります。要点だけ:

### Linux

```sh
sudo dmesg -w          # 挿す前に起動。拒否理由が名指しで出る
lsusb -v -d 303a:      # ホストが解釈したディスクリプタ
```

- `dmesg`はディスクリプタが弾かれた理由と、どのドライバがバインドしたかを
  示します。
- libusb / PyUSB系ツールのpermissionエラーはudevルール（応急なら
  `sudo chmod a+rw /dev/bus/usb/<bus>/<dev>`）で解決します。
  [tests/manual/README.ja.md](../tests/manual/README.ja.md)参照。

### Windows

- USB Device Tree Viewerが生ディスクリプタとエラーの両方を表示します。
- デバイスマネージャー:「Device Descriptor Request Failed」ならディスクリプタ
  段階、Code 10 / Code 43（「!」バッジ）ならドライババインドの失敗です。
- [ディスクリプタキャッシュ](#変更したのにホストで古いデバイスのまま見える)に
  注意してください。
- vendorインターフェースはWinUSBがバインドして初めて開けます。
  `config.webusbEnabled = true`でMS OS 2.0 descriptorを返すか、Zadigで手動
  バインドします。

### macOS

- システム情報 → USBでツリーを、`ioreg -p IOUSB -l -w 0`で詳細を確認します。

## 6. クラス別の問題

### HID keyboard

- **違う文字が入力される** - ホストは自分のレイアウトでscancodeを解釈します。
  `keyboard.setLayout()`で合わせます。
- **何も届かない** - 送信前に`device.ready()`を確認します。未マウント中の
  送信は捨てられます。
- **BIOS / UEFIで効かない** - ホストがboot protocolへ切り替えています。NKROは
  そこでは6キーに畳まれます。
  [`EspUsbDeviceConsole`](../examples/Info/EspUsbDeviceConsole/)で
  `HOST_HID_PROTOCOL`を観測します。
- **Lock LED** - `onOutputReport()`コールバックを付けていなくても
  `ledState()`で読めます。

### USB MIDI

- ホストがEspUsbHostの場合、cable数は方向あたり5本以下にします。それを超えると
  configuration descriptorがホスト側の256バイトcontrol transfer上限を超え、
  ホスト側で列挙に失敗します。

### MSC

- **マウントされない** - FATイメージかブロックサイズの問題です。既知の正常な
  最小イメージである[`MSCFatRamDisk`](../examples/MSCFatRamDisk/)と
  比較します。

### USB Audio

- **音が途切れる** - `playback.read()` / `capture.write()`を一定周期で呼び、
  stream statsを観測します。FIFOは有限です。
- defaultはUAC1で、UAC2は`EspUsbAudioFunction` constructorで明示選択します。
  sample rateは方向ごとに1つ宣言します。

### CDC-NCMネットワーク

- **ホストにネットワークが生えない** - まずホストOSがNCMドライバをバインド
  したかを[`tests/manual/usb_ncm`](../tests/manual/usb_ncm/)で確認します。
  [`UsbNetwork`](../examples/UsbNetwork/) exampleではデバイス側が
  `192.168.7.0/24`でDHCPを提供します（デバイス = `192.168.7.1`）。
- **2.0.xで通信し続けると再起動まで死ぬ** - 2.1.0で修正済みです（DWC2
  driverをDMAモードで駆動）。ライブラリを更新してください。

## 7. 安定性

- **抜き差しを繰り返すと動かなくなる** - 再列挙をまたいで残る状態があります。
  [`tests/manual/enumeration_soak`](../tests/manual/enumeration_soak/)で
  再現し、`onBusDetached()` / `onBusAttached()`で状態を落とします。
- **`begin()`直後にログが死ぬ** - 1コネクタのボードではログの口とデバイスの
  口が同じです（[ガイド2.3](usb-device-guide.ja.md#23-開発中のコネクタ構成)）。

## 8. チップ別の注意

| チップ | 注意点 |
|--------|--------|
| ESP32-S2 | FS controller 1つ。endpoint予算はS3と同クラス（EP番号最大5、非control IN 4本） |
| ESP32-S3 | FS controller 1つ。実機検証の主対象。IN 4本の予算が複合構成の実質上限 |
| ESP32-P4 | FS（rhport 0）と HS（rhport 1）の2つ。`Auto`はHSを選択。FS PHYはUSB-Serial/JTAGとピンを共有し、`usb_wrap_ll_phy_select()`でGPIO24/25へ切り替えると、Serial/JTAGがGPIO26/27へ移る。どのコネクタがどのPHYに繋がるかはボードごとに異なる |
| ESP32 / C3 / C6 / … | USB-OTG device controller非搭載。非対応 |

controllerとは別に、1デバイスに登録できるのは最大**4クラス**で、この上限は
黙って失敗します（[ガイド3.4](usb-device-guide.ja.md#34-その他のライブラリ側の上限)）。

## 9. それでも解決しないとき

1. まずホストのログを読みます。`dmesg -w`、USB Device Tree Viewer、
   Console.app。ホストが何を嫌がったかが最大の情報源です
   （[ガイド5章](usb-device-guide.ja.md#5-ホストosから自分を観測する)）。
2. 同じクラスの市販デバイスとdiffします
   （[ガイド5.5](usb-device-guide.ja.md#55-うまくいっている機器と比べる)）。
3. issueを報告するときは、ライブラリとcoreのバージョン、チップ、
   BringUpCheckの出力、DescriptorDumpの出力、ホストOSのログ抜粋を
   添えてください。この5点でほとんどの問題は一目で再現できます。
