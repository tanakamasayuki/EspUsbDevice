# トラブルシューティング

> English: [troubleshooting.md](troubleshooting.md)

EspUsbDevice v3で起きる問題を、症状から引けるようにまとめた文書です。
基本は[USBデバイス開発ガイド](usb-device-guide.ja.md)、ライブラリの内部は[応用ガイド](usb-device-advanced.ja.md)、
各classの詳細は[classes/](classes/)の各ページにあります。

Hostを調べる前に、次の3つのsketchで多くのことが分かります。

| sketch | 分かること |
| --- | --- |
| [`EspUsbDeviceBringUpCheck`](../examples/Info/EspUsbDeviceBringUpCheck/) | `begin()`が成功したか、Hostが構成したか、どの速度か |
| [`EspUsbDeviceDescriptorDump`](../examples/Info/EspUsbDeviceDescriptorDump/) | ライブラリが作ったdescriptorの全体とendpointの予算。Hostは不要（`DUMP_DESCRIPTORS_ONLY 1`） |
| [`EspUsbDeviceConsole`](../examples/Info/EspUsbDeviceConsole/) | 現在の状態（`ready`、`suspended`、keyboardのLED、HIDのprotocol）と、HID/Vendorの手動送信 |

## 目次

1. [begin() が失敗する](#1-begin-が失敗する)
2. [begin() は成功するがHostに何も出ない](#2-begin-は成功するがhostに何も出ない)
3. [列挙は始まるが途中で失敗する](#3-列挙は始まるが途中で失敗する)
4. [buildと書込み](#4-buildと書込み)
5. [Host OS別の注意](#5-host-os別の注意)
6. [class別の問題](#6-class別の問題)
7. [安定性](#7-安定性)
8. [chip別の注意](#8-chip別の注意)
9. [それでも解決しないとき](#9-それでも解決しないとき)

## 1. begin() が失敗する

### 1.1 まず理由を読む

失敗の理由は必ず`esp_err_t`で記録されます。読む場所は3つです。

| 場所 | 返すもの |
| --- | --- |
| `Function.configure(config)` | `esp_err_t`を直接返す。`ESP_OK`なら設定を保存した |
| `device.begin(config)` | `bool`。理由は`device.lastError()` |
| `Function.startError()` | 直近の`begin()`で、そのFunctionが開始を断った理由 |

```cpp
if (vendor.configure(vendorConfig) != ESP_OK) { /* 設定そのものが誤り */ }
if (!device.begin(config)) {
  Serial.printf("begin: %s, vendor: %s\n",
                esp_err_to_name(device.lastError()),
                esp_err_to_name(vendor.startError()));
}
```

- `device.lastErrorName()`は、`device.lastError()`の`esp_err_to_name()`の名前を返します。
  どのコードでも名前が出ます（HID送信の`ESP_ERR_TIMEOUT`も）。
  Functionの`startError()`はただの`esp_err_t`なので、`esp_err_to_name()`で表示します。
- Functionが失敗したとき、`device.lastError()`はそのFunctionの`startError()`と同じです。
  すでに開始したFunctionは終了し直され、確保したものは残りません。
- 失敗した`begin()`の後、deviceは停止状態です。設定を直して`begin()`を呼び直せます。resetは要りません。

### 1.2 errorコードごとの原因

確認は次の順に行います。deviceの設定、descriptorとendpointの予算、各Functionの資源、最後にPHY・controller・USB worker。
PHYより前の段階ではbusに触れないので、Hostには何も見えません。

#### `ESP_ERR_INVALID_SIZE`（0x104）：入りきらない

| 原因 | 対処 |
| --- | --- |
| 非controlのIN endpointがcontrollerの数を超えた。ESP32-S2/S3とESP32-P4のFS controllerは4本、P4のHS controllerは7本。endpoint番号にも上限がある（S2/S3は1〜6） | DescriptorDumpで数える（Host不要）。下の[予算の表](#endpointの予算)を参照。Functionを減らすか、P4のHS controllerを使う |
| `bulkInBuffering = EspUsbBulkInBuffering::Double`で、すべてのbulk INに2 packet分のTX FIFOを取れない | `Auto`（全部入るときだけ倍にする）か`Single`にする |
| 構成全体のIN endpointが、controllerのTX FIFOに入らない（S2/S3/P4 FSは256 word、P4 HSは1024 word。EP0とRX FIFOと共有）。P4のHS controllerでは、既定のVendor（bulk IN 512 byte）は5個まで入り、6個目は入らない | IN endpointを減らすか、packetを小さくする（Audio/Videoのisochronous packet、`EspUsbDeviceHidCustom`の`inputReportSize`） |
| MSCの`transferBytes`がbulkのMPS（FSは64、HSは512）の倍数でない | 512の倍数にする。FSでもHSでも使える |
| CCIDの`messageBytes`を、Bulk-OUTのMPS（HSでは512）未満の値で明示した | 0（自動：`max(271, MPS)`）のままにするか、512以上にする |
| Vendorのfull speed endpoint長が8、16、32、64のどれでもない（64を超える値は64として扱う。Functionの`startError()`に出る） | このどれかにする（USB 2.0 §5.8.3） |
| HID：まとめたHIDのinterrupt packetがFSで64、HSで1024 byteを超えた。report IDが足りない。まとめたreport descriptorが65535 byteを超えた。`EspUsbDeviceHidVendor`の`reportSize`が`maxReportSize()`を超えた | HID Functionを減らすか小さくする |
| descriptorの文字列がUTF-16で126単位を超えた。WebUSB URLが`http(s)://`を除いて252 byteを超えた | 短くする |
| Audio：外部ring（`playbackBuffer`/`captureBuffer`）が「frame数×最大のPCM frame」より小さい | `playbackBufferBytes`/`captureBufferBytes`に実際の`sizeof()`を渡すか、ライブラリに確保させる |
| `taskStackBytes`がFreeRTOSの最小値より小さい | 増やす（既定4096） |
| 1つのdeviceに251を超えるFunction | - |

範囲外の値には`configure()`自身が`ESP_ERR_INVALID_SIZE`を返します。[1.3](#13-configure-が設定を拒否する)を参照。

#### `ESP_ERR_INVALID_ARG`（0x102）：設定どうしが矛盾している

| 原因 | 対処 |
| --- | --- |
| `config.deviceInterfaceGuid`が波括弧付きの`{8-4-4-4-12}`（38文字）でない | GUIDを直す |
| `manufacturer`/`product`/`serialNumber`、Functionの名前、WebUSB URLが正しいUTF-8でない。URLが`http(s)://`の後で空 | 文字列を直す |
| `msOs20Layout = ESP_USB_DEVICE_MS_OS_20_FLAT`で、WinUSBを使うinterfaceが2つ以上ある（VendorとDFUがそれぞれ1つ） | `ESP_USB_DEVICE_MS_OS_20_AUTO`にする |
| `taskPriority`や`taskCoreId`が範囲外。未知の`startMode` | -1（既定）か有効な値にする |
| bulkのFunctionが`EspUsbBulkRxMode::Callback`なのに`onRxData()`がない、`Borrowed`なのに`onDirectRxComplete()`がない。MIDIが`EspUsbMidiRxMode::Callback`なのに`onPacket()`がない | `begin()`の前にcallbackを登録する |
| MIDIの容量や`transferBytes`が4 byteの倍数でない | packet単位で数える |
| MSCに保存先がない。`attach()`を呼んでいない（容量か`onRead`がない） | `device.begin()`の前に`disk.attach(msc)`、または`onRead`と`msc.begin(blockCount, blockSize)` |
| Audio：streamがない、formatのないstreamがある、同じ向きでchannel数が違う、共通clockでsample rateの組が違う | formatをそろえる（[Audio](classes/audio.ja.md)を参照） |
| `EspUsbDeviceHidCustom`のreport descriptorがない、または解析できない | descriptorのbyte列を確かめる |
| Videoのframeの大きさ、rate、最大frame長が範囲外 | `setFrameSize()`は4096x4096まで |
| 外部bufferどうしが重なっている。DMA可能でなければならないbuffer（Videoの`payloadBuffer`、NCMのbuffer）がそうでない | 別々の内部RAMの領域を渡すか、`nullptr`にする |

#### `ESP_ERR_INVALID_STATE`（0x103）：順序の誤り、または使用中

| 原因 | 対処 |
| --- | --- |
| 開始中に`begin()`を呼んだ | 先に`end()` |
| 別の`EspUsbDevice` objectが動いている | controllerごとにdeviceは1つ |
| deviceの稼働中にFunctionを作った（登録した） | Functionは`begin()`の前に宣言する。または`end()`→作成→`begin()` |
| CCIDが`removable = false`で、cardを挿していない | `begin()`の前に`insertCard()` |
| PHYやcontrollerが使用中（Core自身のUSB stackなど） | [4. buildと書込み](#4-buildと書込み)を参照 |

Function自身の`begin()`、`afterDeviceStarted()`、`prepareToStop()`はprotectedです。
呼ぶのは`device.begin()`と`device.end()`だけです。
sketchで`console.begin()`や`keyboard.begin()`を書くとcompile errorになります。
容量を設定するだけの`msc.begin(blockCount, blockSize)`と、helper自身の`begin()`（`EspUsbDeviceMscSdCard::begin()`など）は公開のままです。

#### `ESP_ERR_NO_MEM`（0x101）

- Functionのbuffer、descriptorの領域、USB workerを確保できなかった。確保済みのものは戻します。
  容量（`rxCapacity`、`txCapacity`、ringのframe数、`messageBytes`、NTBの大きさ）を減らしてください。
- TX FIFOが足りないときは、このコードではなく`ESP_ERR_INVALID_SIZE`です。

#### `ESP_ERR_NOT_SUPPORTED`（0x106）

- USB OTG controllerのないchip。対応はESP32-S2、ESP32-S3、ESP32-P4だけで、ESP32、C3、C6、H2、S31は対象外です。
- HS controllerのないchip（S2/S3）で`config.controller = EspUsbController::HighSpeed`を指定した。

#### `ESP_FAIL`とその他

- `ESP_FAIL`：controllerの初期化に失敗した。またはcontrollerの開始後にFunctionが失敗した。
  Core Debug LevelをVerboseにしてESP-IDFのlogを読んでください。
- ESP-IDFのPHY driver（`usb_new_phy()`）のerrorは、そのまま返します。

### 1.3 configure() が設定を拒否する

`configure()`は渡された設定だけを検証します。この時点では速度が決まっていないので、
速度で変わる確認（MPSの倍数など）は`begin()`で行います。

| Function | `ESP_ERR_INVALID_SIZE` | `ESP_ERR_INVALID_ARG` |
| --- | --- | --- |
| CDC / Vendor（`EspUsbBulkStreamConfig`） | - | `rxCapacity`は`Stream`では0以外、ほかのmodeでは0が必要。TX ringがあるのに`transferBytes`が0。未知のmode。外部bufferの重なりやaddressのあふれ |
| MIDI（`EspUsbMidiConfig`） | `transferPackets`が16383を超える | cableが1〜16の外。`Queue`以外のmodeで`rxPackets`/`rxBuffer`を指定 |
| MSC（`EspUsbMscConfig`） | `transferBytes`が0、65535超、または64の倍数でない | `transferBuffer`が64 byte境界でない、またはDMA可能な内部RAMでない（PSRAMは拒否） |
| CCID（`EspUsbCcidConfig`） | `messageBytes`が0以外で64〜65535の外 | - |
| NCM（`EspUsbNetConfig`） | NTBの大きさが2048〜65535の外、`maxFrameBytes + 28`がNTBより大きい、`maxDatagrams`が0か大きすぎる | 外部bufferを渡したのに個数やbyte数がない |
| Audio（`EspUsbAudioConfig`） | 大きさの計算があふれる | buffer pointerとbyte数の片方だけを指定。未知のoverflow方針 |
| Video（`EspUsbVideoConfig`） | `payloadBytes`が3〜65535（isochronousは3〜1024）の外。`isochronousPacketBytes`が3〜1024の外 | 未知のtransport |
| DFU（`EspUsbDfuConfig`） | `transferBytes`が64〜65535の外。poll時間が0xFFFFFFを超える | `transferBuffer`のaddressがあふれる |

どれもdeviceの開始中は`ESP_ERR_INVALID_STATE`を返します。

### 1.4 callbackの登録や設定がESP_ERR_INVALID_STATEになる

callbackの登録（`onRx`、`onLineCoding`、`onOutputReport`、`onPacket`、`onRead`、`onProgress`など）と、
`vendorID()`、`setName()`、`macAddress()`などの設定は、**deviceの停止中だけ**使えます。
`device.begin()`の後は`ESP_ERR_INVALID_STATE`を返し、何も変えません。
戻り値を見ないsketchでは、callbackが呼ばれないだけに見えます。
すべて`begin()`の前に登録するか、`end()`→登録→`begin()`の順にしてください。

## 2. begin() は成功するがHostに何も出ない

`begin()`が`true`を返すのは、構成が正しく資源を確保できたという意味です。Hostが列挙したという意味ではありません。
次の順に確かめます。

1. **connectorが違う。** USB-UARTやUSB Serial/JTAGのconnectorを別に持つboardが多く、それはOTG controllerにつながっていません。
   印刷ではなく回路図で確かめます。
2. **充電専用のcable。** data通信できると分かっているcableに替えます。
3. **ESP32-S2/S3のUSB Mode。** *USB Mode: USB-OTG (TinyUSB)*（FQBNでは`USBMode=default`）でbuildします。
   ESP32-S3のIDEの既定は「Hardware CDC and JTAG」で、共有のPHYをUSB Serial/JTAGに渡してしまいます。
   [4. buildと書込み](#4-buildと書込み)を参照。
4. **ESP32-P4：controllerとconnectorが合っていない。** `Auto`はHS controllerを選びます。
   cableがFSの口なら`config.controller = EspUsbController::FullSpeed`、HSのconnectorなら`Auto`か`HighSpeed`にします。
5. **Functionを1つも登録していない、または`DescriptorsOnly`。** Functionがない場合や
   `config.startMode = EspUsbDeviceStartMode::DescriptorsOnly`では、`begin()`は成功して`started()`もtrueですが、
   controllerは起動せず、`ready()`はfalseのままです。
6. **VBUS検出のないboardで`config.vbusSensing = true`。** boardがVBUSをcontrollerへ配線していないなら`false`のままにします。
7. ここまで確かめたら、BringUpCheckを動かし、[Host側のlog](#5-host-os別の注意)を読みます。
   この先の原因はHost側からしか見えません。

## 3. 列挙は始まるが途中で失敗する

### Windowsに「不明なUSBデバイス（デバイス記述子要求の失敗）」と出る

descriptorを読む段階で失敗しています。

1. [USB Device Tree Viewer](https://www.uwe-sieber.de/usbtreeview_e.html)で、descriptorの生のbyte列とWindowsのerrorを読みます。
2. そのbyte列をDescriptorDumpの出力と比べます。Linuxでは`lsusb -v`で、Hostが解釈したdescriptorを見られます。

### 変更したのにHostに古いdeviceが見える

**WindowsはVID/PID/serialごとにdescriptorとpropertyを記憶します。** 「変更が効かない」の一番の原因です。

1. 開発中にdescriptorを変えるときはPIDを変える。
2. または`config.serialNumber`を変える。
3. またはDevice Managerでdeviceを削除して挿し直す。

Microsoft OS 2.0 descriptorは`wVendorRevision`が変わったときだけ読み直されます。
既定の`msOs20VendorRevision = 0`はsetのbyte列から値を作るので、setが変われば自動で変わります。

### `SET_CONFIGURATION`は通るがdriverが付かない

descriptorの形は正しいものの、class/subclass/protocolの組がOSのdriverに対応していません。
同じclassの市販品と比べてください。interfaceの順、IAD、`bInterfaceProtocol`、endpointのintervalがよく違う点です。

### 複合deviceが1つの機能としか見えない

Windowsが機能ごとにdriverを付けるのは、usbccgp.sysが親になったときだけです。
それには`bDeviceClass/SubClass/Protocol = 0xEF/0x02/0x01`が必要で、ライブラリは構成にInterface Association Descriptorがあると自動で設定します
（CDC ACM、NCM、Audio、VideoがIADを出します）。
それでも1つに見えるなら、以前のfirmwareの記憶が残っている可能性が高いです。上の「古いdevice」の項を参照してください。
`config.msOs20CcgpDevice = true`にすると、interfaceが1つのdeviceも複合deviceとして扱うようWindowsへ求めます。

## 4. buildと書込み

1. **Arduino-ESP32 3.3.12**を基準にしています。examplesの`sketch.yaml`はこの版に固定しています。
2. **USB Mode。** ESP32-S2/S3は*USB-OTG (TinyUSB)*（`USBMode=default`）でbuildします。
   ESP32-P4のHS controllerは専用のPHYを持ちます。P4Xでは、EspUsbDeviceをHS口で動かしたまま、
   *Hardware CDC and JTAG*（`USBMode=hwcdc`、`CDCOnBoot=cdc`）でlogをUSB Serial/JTAGへ出して使っています。
   P4のFS controllerを使うときは`USBMode=default`にします。FSのPHYはUSB Serial/JTAGと共有で、hwcdcとの組合せは未確認です。
3. **CoreのUSB device stackを同じsketchで使わない。** `USB.begin()`、`USBHIDKeyboard`、`USBCDC`、`USBMSC`などを使わず、
   *USB CDC On Boot*、*USB MSC On Boot*、*USB DFU On Boot*をOTG controllerに対して有効にしません。
   どちらも同じcontrollerを動かそうとします。[Arduino-ESP32標準USB APIからの移行](migrating-from-arduino-esp32-usb.ja.md)を参照。
4. **`Serial`はOTGの口に出ない。** 上の設定では`Serial`はUART（hwcdcならUSB Serial/JTAG）に出ます。
   OTGのconnectorしかないboardでは、logとdeviceが同じ口になります。開発中はconnectorが2つあるboardか、UART adapterを使います。
5. **`#error "Remove TinyUSB build flags..."`。** v3にはbuild時の容量flagがありません。
   `CFG_TUD_*`のbuffer長やv2の`ESP_USB_DEVICE_*` flagはcompile errorになります。`build_opt.h`から消し、
   容量は各Functionの`configure()`で指定します。`ESP_USB_DEVICE_CCID_BUFFER_SIZE`も同様で、`EspUsbCcidConfig::messageBytes`へ移します。
6. **headerは見つかるのに`undefined reference to EspUsbDeviceVendor::...`。** `sketch.yaml`の`libraries: - dir:`がsymlinkを指していると、
   arduino-cliがheaderは見つけても、その先の`.cpp`をcompileしないことがあります。実際のpathかLibrary Managerを使います。

## 5. Host OS別の注意

v3はESP32のHost（EspUsbHost）とLinux PCで確認しています。WindowsとmacOSはv3では未確認です。
下のWindowsの注意はv2での確認に基づきます。Microsoft OS 2.0の構成はv3でも変えていません。

### Linux

```sh
sudo dmesg -w          # 挿す前に始める。拒否の理由が出る
lsusb -v -d 303a:      # Hostが解釈したdescriptor（自分のVIDを指定）
```

- `dmesg`は、descriptorを拒否した理由と、付いたdriverを表示します。
- libusb/PyUSBの道具が権限errorで失敗するときは、そのVID/PIDのudev規則が要ります。
- `dmesg`で足りないときは、`usbmon`とWiresharkでcontrol転送を見ます。

### Windows（v3では未確認）

- USB Device Tree Viewerで生のdescriptorとerrorを見ます。Device Managerの「デバイス記述子要求の失敗」はdescriptorの段階、
  Code 10/Code 43はdriverの段階の失敗です。
- [descriptorの記憶](#変更したのにhostに古いdeviceが見える)に注意します。
- **INFなしでWinUSBが付きます。** ライブラリはVendorとDFUの各interfaceに対して、Microsoft OS 2.0 descriptorを自動で出します。
  `config.webusbEnabled`はWebUSBの能力を足すだけで、WinUSBのためには要りません。
- **vendor interfaceがCode 28（`CM_PROB_FAILED_INSTALL`）で、`setupapi.dev.log`に何も出ない。** compatible IDがdevice nodeへ届いていません。
  function subsetはusbccgp.sys経由でしか解決されず、Windowsはそれを複合deviceにしか読み込みません。
  interfaceが1つのdeviceでは、compatible IDをsetのheader直下に置く必要があります。
  既定の`ESP_USB_DEVICE_MS_OS_20_AUTO`はinterface数で正しい形を選びます。`device.microsoftOs20UsesSubsets()`で確かめられます。
  Hostの要求を見るには`device.onAnyControlRequest()`を使います。ライブラリが自分で答える要求も見えます。
- **同じ名前のCOM portが2つ並ぶ。** Windowsは複合deviceの子に、その機能の`iFunction`文字列で名前を付けます。
  Functionごとに名前を付けます：`EspUsbDeviceCdcSerial serial(device, "Console");`
- **差すportでCOM番号が変わる。** WindowsはVID/PID/serialで番号を決めます。boardごとに違う`config.serialNumber`を付けます。
- **Host側のアプリが無関係なdeviceまで見つける。** 製品ごとに`config.deviceInterfaceGuid`を付けます。

### macOS（v3では未確認）

- システム情報→USBで木構造、`ioreg -p IOUSB -l -w 0`で詳細を見ます。

## 6. class別の問題

### HID（全HID Function共通）

- 1つのdeviceのHID Functionはすべて、report IDを付けて**1つのHID interface**にまとめます。IN endpointは1本です。
- 送信（`sendReport()`、`move()`、`tapKey()`など）は、IN endpointが空くまで`timeoutMs`（既定100 ms）待ちます。
  `0`は1回だけ試します。空かなければ`false`で`ESP_ERR_TIMEOUT`、Hostが構成していなければ`ESP_ERR_INVALID_STATE`です。
  成功はreportをcopyして送信待ちに入れたという意味で、Hostが読んだという意味ではありません。
- **USBのcallbackの中では`timeoutMs = 0`にします。** USB worker上で0以外を指定すると、すぐに`ESP_ERR_INVALID_STATE`で失敗します。
  そこで待つと、endpointを空けるはずのworker自身が止まるためです。

### HID keyboard

- **違う文字が出る。** Hostは自分のlayoutでキーの位置を文字に直します。`keyboard.setLayout()`をHostに合わせます。
- **`write()`が途中で止まる。** layoutで打てない文字があると、そこで`false`（`ESP_ERR_NOT_SUPPORTED`）を返し、残りは送りません。
  `pressKey()`、`tapKey()`、`releaseKey()`も同じです。
- **7つ目のキーが失敗する。** `pressUsage()`/`pressKey()`は、押しているキーと修飾キーに足し込みます。
  NKROなしのreportは、修飾キー以外を6つまで持てます（HID 1.11 Appendix B.1）。
  7つ目は`false`（`ESP_ERR_INVALID_SIZE`）で、状態は変わりません。もっと押すには、`begin()`の前に`enableNkro()`を呼びます。
  NKROでは、bitmapで表せないusage（0xDFより上で修飾キーでないもの）が`ESP_ERR_INVALID_ARG`です。
- **tapの前から押していたキーは押したまま。** `tapUsage()`/`tapKey()`は、そのtapで足したキーと修飾キーだけを離します。
  ほかのキーを離すには`releaseUsage()`、`releaseKey()`、`releaseAll()`を使います。
- **BIOS/UEFIで効かない。** Hostがboot protocolに切り替えています。NKROはそこで6キーにまとめられます。
  `onProtocol()`か`protocol()`で確かめます。
- **Lock系のLED。** `ledState()`は`onOutputReport()`がなくても更新され、bus resetで消えます。

### HID mouse

- `move(x, y)`は、`press()`で押したbuttonを保ちます。`press()`の後に`move()`でdragになります。
  4番目の引数の既定は`EspUsbDeviceHidMouse::KeepButtons`（-1）です。0〜255を渡すと、押しているbuttonをその値に置き換えます。
  それ以外の値は`ESP_ERR_INVALID_ARG`です。

### HID consumer controlとsystem control

- Consumerのusageが0x3FF（reportのLogical Maximum）を超えると、`false`（`ESP_ERR_INVALID_ARG`）で、何も送りません。
- System Controlが受け付けるのは0x81、0x82、0x83と、離すための0だけです。ほかの値も同じく失敗します。

### CDC ACM

- **`print()`が1秒止まる。** TX ringが満杯のとき（Hostが読んでいない、terminalを開いていない）、
  `write()`/`print()`はTX timeout（既定1000 ms）まで待ちます。`setTxTimeoutMs(0)`にすると、送れた分だけですぐ戻ります。
- `connected()`がtrueになるのは、Hostがportを開いてDTRを立てている間だけです。
- CDCのportはsketchのdata用で、`Serial`ではありません。

### Vendor bulk

- **Hostのreadが終わらない。** streamの書込みは、既定では短いpacketで終わりません。MPSの倍数の書込みにZLPも付けません。
  Host側で長さを決めて読むか、`streamTermination = EspUsbStreamTermination::EndWithShortPacket`にします。
- `writeDirect()`/`readDirect()`のbufferは、64 byte境界の、DMA可能な内部RAMが必要です。PSRAMのbufferは`NotDmaCapable`で拒否します。
  `rxBuffer`/`txBuffer`で渡すringはPSRAMでもかまいません。

### USB MIDI

- **channelが先頭で、0始まり**です（`noteOn(channel, note, velocity, cable)`）。cable番号は最後で、`inCableCount()`未満にします。
- **messageが送られない。** channelが15超、data byteが127超、pitch bendが16383超のときは丸めません。
  `false`（`ESP_ERR_INVALID_ARG`）を返し、何も送りません。
- Callback modeでは、`begin()`の前に`onPacket()`を登録します。

### MSC

- **driveが出ない。** `EspUsbDeviceMscRamDisk`と`EspUsbDeviceMscFatRamDisk`は媒体なしで始まります。
  `device.begin()`の前に`msc.mediaPresent(true)`を呼びます。`EspUsbDeviceMscFirmwareDisk::attach()`は媒体ありと書込み可を自分で設定します。
- `attach()`は`device.begin()`の前に呼びます。呼ばないと`begin()`が`ESP_ERR_INVALID_ARG`で失敗します。
- **挿し直した直後の最初のcommandが失敗する。** Hostが構成済みのときに`mediaPresent(true)`にすると、
  SCSIの決まりどおりUNIT ATTENTION（sense 06/28/00）を1回返します。次のTEST UNIT READYは成功します。
- **媒体ありでもmountできない。** FATのimageとblock長を、動作が確かな最小imageの[`MSCFatRamDisk`](../examples/MSCFatRamDisk/)と比べます。
- **Hostが「書込み禁止」と言う。** `msc.isWritable(false)`や`EspUsbDeviceMscSdCard::readOnly(true)`は、書込み禁止としてHostへ伝えます。
  MODE SENSEのWPが立ち、WRITEはDATA PROTECT（sense 07/27/00）で失敗します。
- **`EspUsbDeviceMscSdCard::attach()`が`false`。** cardがmountされていない、sectorが512 byteでない、callbackを登録できない、またはdeviceが稼働中です。

### USB Audio

- **`begin()`が`ESP_ERR_INVALID_ARG`。** streamの設定が食い違っています。[1.2](#esp_err_invalid_arg0x102設定どうしが矛盾している)の表を参照。
- **`begin()`が`ESP_ERR_INVALID_SIZE`。** isochronous INのendpoint（capture、またはUAC2のfeedback）とほかのIN endpointが、合わせてTX FIFOに入りません。
- **音が途切れる。** `playback.read()`/`capture.write()`を一定の間隔で呼び、`stats()`を見ます。
  playbackのringがあふれると、既定の`DropOldest`は古い未読のPCMを捨てます。
- 既定はUAC1です。UAC2は`EspUsbAudioFunction`のconstructorで選びます。
  UAC2のfeedbackは、FSでは10.14形式の3 byte、HSでは16.16形式の4 byteです（USB 2.0 §5.12.4.2）。

### USB Video

- format、frameの大きさ、rate、最大frame長は`begin()`で決まり、その後の設定は拒否されます。
- FSのisochronous endpointは1 ms frameあたり最大1023 byte（約1 MB/s）です。小さな試験画像より大きいものはMJPEGにします。

### CDC-NCM network

- **Hostにaddressが配られない。** DHCP serverは既定でoffです。`net.beginNetwork()`の前に`net.dhcpServer(true)`を呼び、
  `beginNetwork()`は`device.begin()`の後に呼びます。既定のaddressは`192.168.7.1/24`です。
  指定しない限り、gatewayもDNSも通知しません。
- PCのOSのNCM driverとの相互接続は、v3では未確認です。

### CCID

- `messageBytes = 0`（既定）は`max(271, Bulk-OUTのMPS)`になります。FSでは271、HSでは512です。MPS未満を明示すると`begin()`が失敗します。
- `removable = false`では、`begin()`の前に`insertCard()`が必要です。

### DFU

- `onDetach()`と`setDetachHandler()`はRuntime mode専用です。Download modeでは`ESP_ERR_INVALID_ARG`を返します。
- Downloadの失敗はDFUのstatus（`EspUsbDfuStatus`）で、`onError()`に届きます。[USB経由のファームウェア更新](ota-over-usb.ja.md)を参照。

## 7. 安定性

- **`end()`が`ESP_ERR_INVALID_STATE`を返す。** USBのcallbackから呼んでいます。callbackではflagを立て、`loop()`から`end()`を呼びます。
- **`end()`が`ESP_ERR_TIMEOUT`を返す。** USB workerが、実行中のcallbackを`config.stopTimeoutMs`（既定5000 ms）のうちに終えませんでした。
  停止の要求は残り、資源も保持します。DMAがまだbufferに触るかもしれないためです。
  callbackが戻ったら、もう一度`end()`を呼びます。`stopTimeoutMs = 0`は無期限に待ちます。
  destructorとFunctionの`end()`は、停止が終わるまで必ず待ちます。
- **動作中に設定を変えたい。** `end()`→`configure()`→`begin()`の順です。Hostからは抜いて挿し直したように見え、deviceを開き直す必要があります。
  接続したままdescriptorは変えられません。
- **挿し直しても状態が残る。** VBUS検出がないと、抜いたことは次の列挙まで分かりません。
  Hostに関する状態は、`ready()`がfalseになったときではなく、再びtrueになったときに消してください。
- **busがsuspendしている。** suspend中も`ready()`はtrueのままです。`suspended()`で確かめます。suspendしても構成は失われません。
  `remoteWakeup()`は、`remoteWakeupSupported = true`で、Hostが許可したときだけ使えます。
- **callbackが重い。** class callbackはすべてUSB worker上で動き、既定ではFreeRTOSの最高priorityです。
  そこで長い処理をすると、ほかのFunctionがすべて遅れます。自分のtaskへ渡してください。
  callbackでたくさん表示するなら`taskStackBytes`を増やします。
- **`begin()`の直後にlogが止まる。** connectorが1つのboardでは、logの口とdeviceの口が同じです。

## 8. chip別の注意

| chip | USB controller | 注意 |
| --- | --- | --- |
| ESP32-S2 | FS | endpointの予算はS3と同じ。**現時点ではcompileのみ**で、v3の実機確認はしていない |
| ESP32-S3 | FS | 実機確認の中心。PHYは1つでGPIO19/20、USB Serial/JTAGと共有 |
| ESP32-P4（v1.x） | HS（480 Mbps、専用のUTMI PHY）とFS | `Auto`はHS。`ChipVariant=prev3`でbuild |
| ESP32-P4X（v3.x） | HSとFS | USBの予算はv1.xと同じ。`ChipVariant=postv3`でbuild。SDKの対応はv3.1以上 |
| ESP32-S31 | - | まだ対応していない |
| ESP32、C3、C6、H2など | なし | USB OTG controllerがない |

### endpointの予算

| controller | endpoint番号 | 非controlのIN endpoint | TX/RX FIFO |
| --- | --- | --- | --- |
| S2 / S3 / P4 FS | 1〜6 | 4 | 256 word |
| P4 HS | 1〜15 | 7 | 1024 word |

Functionごとの非control IN endpointの数：

| Function | IN | Function | IN |
| --- | --- | --- | --- |
| HID（全HID Functionで合わせて） | 1 | NCM | 2 |
| CDC ACM | 2 | CCID（`removable = true`） | 2 |
| Vendor、MIDI、MSC、Video | 各1 | CCID（`removable = false`） | 1 |
| Audioのcapture | 1 | DFU | 0 |
| AudioのUAC2 playback（feedback） | 1 | Audioのplayback（OUT） | 0 |

S3では、HID＋CDC＋MSC（1＋2＋1）で4本を使い切ります。DFUはEP0だけを使うので、いつでも追加できます。

### High Speed（P4）で変わる値

| 項目 | FS | HS |
| --- | --- | --- |
| bulkのMPS | 8/16/32/64 | 512 |
| MSCの`transferBytes` | 64の倍数 | 512の倍数 |
| CCIDの自動`messageBytes` | 271 | 512 |
| 借用RX（`readDirect`）の容量 | 64の倍数 | 512の倍数 |
| HIDのinterrupt packet | 64まで | 1024まで |
| UAC2のfeedback | 10.14、3 byte | 16.16、4 byte |

CDC、Vendor、MIDIの`transferBytes`はMPS未満でも受け付けます。FSで動く構成がHSでは`begin()`で失敗することがあるので、戻り値を確かめてください。
P4で`Auto`か`HighSpeed`を選ぶと、linkがFSに落ちてもHSの制約がかかります。HSのdescriptorも正しくなければならないためです。
Hostと決まった速度は`device.speed()`で分かります（`EspUsbBusSpeed::Full`か`High`。最初のbus resetの前は`None`）。

### 実機での確認範囲

| 構成 | 状態 |
| --- | --- |
| S3 ↔ S3（FS） | 主な試験環境。全10 class |
| 1枚のP4でHS ↔ FSのloopback | 全10 class、両controller |
| P4 ↔ P4XのHS直結（両方向） | 確認済み |
| P4X → Linux PC（HS） | MSCとDFUをpyusbでwireの段階まで確認 |
| ESP32-S2 | compileのみ |
| Windows、macOS | 未確認 |

## 9. それでも解決しないとき

1. まずHostのlogを読みます。`dmesg -w`、USB Device Tree Viewer、Console。Hostが何を拒んだかが、一番役に立つ情報です。
2. 同じclassの市販品と比べます。
3. 問題を報告するときは次を添えてください。ライブラリとCoreの版、chipとrevision、BringUpCheckの出力、DescriptorDumpの出力、
   `esp_err_to_name(device.lastError())`と各Functionの`startError()`、Hostのlogの抜粋。
