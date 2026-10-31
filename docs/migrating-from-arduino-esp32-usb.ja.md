# Arduino-ESP32標準USB APIからの移行

> English: [migrating-from-arduino-esp32-usb.md](migrating-from-arduino-esp32-usb.md)

Arduino-ESP32のCoreに付属するUSB device API（`USB.begin()`、`USBHIDKeyboard`、`USBHIDMouse`、`USBCDC`、`USBMSC`、`USBMIDI`、`USBVendor`など）で
書いたsketchを、EspUsbDevice v3へ移すための文書です。
EspUsbDevice v3は、そのstackと**意図して互換にしていません**。sketchがcontroller、descriptor、各Functionのbuffer、生のreportを明示し、
どの段階でも失敗の理由を返します。この文書では、Coreの各APIに対応するv3のAPIと、どちらか一方にしかないものをまとめます。

EspUsbDevice 2.xから移る場合は[v2からの移行](migrating-from-v2.ja.md)を見てください。

## 考え方の違い（最初に読む）

| | Arduino-ESP32 Core | EspUsbDevice v3 |
| --- | --- | --- |
| object | 全体で1つの`USB`と、classごとのobject | 1つの`EspUsbDevice`（**Device**）と、機能ごとの**Function**のobject。Functionはconstructorで自分をDeviceに登録する |
| 開始 | 各classの`begin()`の後に`USB.begin()` | 必要ならFunctionを`configure()`し、`device.begin(config)`を1回呼ぶ。Functionには公開の`begin()`がなく、呼ぶとcompile errorになる |
| 停止 | - | `device.end()`がすべてを解放する。`end()`→`configure()`→`begin()`で、再buildなしに設定を変えられる |
| VID/PID/文字列 | `USB.VID()`、`USB.productName()`など | `EspUsbDeviceConfig`のfieldに入れ、`device.begin()`へ渡す |
| bufferの大きさ | Coreで固定、またはbuild flag | Functionごとに`configure()`で実行時に決める。旧`CFG_TUD_*` flagはcompile error |
| callback | `onEvent()`をいつでも | 型付きのcallback（`onLineCoding`、`onOutputReport`など）を`device.begin()`の**前に**登録する |
| deviceの状態 | `USB.onEvent(ARDUINO_USB_*_EVENT)` | `device.started()`、`device.ready()`、`device.suspended()`を調べる |
| error | ほとんど知らせない | `configure()`は`esp_err_t`を返す。`begin()`は`bool`を返し、理由は`device.lastError()` |
| 複合device | classを足すと暗黙に重なる | Functionはいくつでも置ける。endpointの予算は`begin()`の中でPHYの開始前に確かめる |
| controller | chipごとに固定 | ESP32-P4では`config.controller`でFS/HSを選ぶ |

### 2つのstackは同時に使えない

EspUsbDeviceは、自前のdriverでUSB OTG controllerを動かします。CoreのUSB device stackも同じcontrollerを動かします。
EspUsbDeviceのsketchでは次を守ります。

- `USB.begin()`を呼ばず、`USBHID*`、`USBCDC`、`USBMSC`、`USBMIDI`、`USBVendor`、`FirmwareMSC`のobjectを作らない。
- ESP32-S2/S3は*USB Mode: USB-OTG (TinyUSB)*（`USBMode=default`）でbuildする。
- *USB CDC On Boot*、*USB MSC On Boot*、*USB DFU On Boot*を、OTG controllerに対して有効にしない。
- そのため`Serial`はOTGの口には出ません。UART（ESP32-P4を`USBMode=hwcdc`でbuildしたならUSB Serial/JTAG）に出ます。
  [トラブルシューティングの4](troubleshooting.ja.md#4-buildと書込み)を参照。

### 移行の手順

1. `#include "USB.h"`とclassのheaderを消し、`#include <EspUsbDevice.h>`にする。
2. `EspUsbDevice device;`を1つ作り、各Functionのconstructorへ渡す。Functionは`device.begin()`より前に宣言する。
3. VID、PID、文字列を`EspUsbDeviceConfig`へ移す。
4. classの`begin()`と`USB.begin()`をすべて、1回の`device.begin(config)`に置き換える。
   `msc.begin(blockCount, blockSize)`は残す（容量を設定するだけ）。
5. callbackはすべて`device.begin()`の前に登録し、返る`esp_err_t`を確かめる。
6. 送信は`device.ready()`がtrueの間だけ行う。
7. `begin()`の結果を確かめ、`esp_err_to_name(device.lastError())`を表示する。
8. 試験中はPID（またはserial番号）を変え、WindowsがCoreのstackのdeviceについて記憶した内容を使わないようにする。

## USB object → EspUsbDeviceConfig

| Core | v3 | 注意 |
| --- | --- | --- |
| `USB.VID(v)` / `USB.PID(p)` | `config.vid` / `config.pid` | 既定は`0x303a` / `0x4000`。机上の試験用で、製品には使わない |
| `USB.manufacturerName()` / `productName()` / `serialNumber()` | `config.manufacturer` / `product` / `serialNumber` | UTF-8。それぞれUTF-16で126単位まで |
| `USB.firmwareVersion(v)` | `config.deviceVersion` | `bcdDevice`。既定`0x0100` |
| `USB.usbPower(mA)` | `config.maxPowerMilliamps` | 既定100 |
| `USB.usbAttributes(...)` | `config.selfPowered`、`config.remoteWakeupSupported` | |
| `USB.usbVersion()` | - | `bcdUSB`は0x0200。BOS descriptorがあるときは0x0201 |
| `USB.usbClass()` / `usbSubClass()` / `usbProtocol()` | - | 自動で決める。IADがあれば`0xEF/0x02/0x01` |
| `USB.webUSB(true)` / `USB.webUSBURL(url)` | `config.webusbEnabled` / `config.webusbUrl` | |
| `USB.onEvent(...)` | `device.ready()`、`device.suspended()`、`device.started()` | device全体のevent callbackはない |
| `USB.begin()` | `device.begin(config)` | `bool`を返す。理由は`device.lastError()` |

```cpp
EspUsbDeviceConfig config;
config.vid = 0x303a;
config.pid = 0x4001;
config.manufacturer = "Example";
config.product = "My Keyboard";
config.serialNumber = "0001";
if (!device.begin(config)) {
  Serial.println(esp_err_to_name(device.lastError()));
}
```

## Keyboard：`USBHIDKeyboard` → `EspUsbDeviceHidKeyboard`

移行前：

```cpp
#include "USB.h"
#include "USBHIDKeyboard.h"
USBHIDKeyboard Keyboard;

void setup() {
  Keyboard.begin();
  USB.begin();
}
void loop() {
  Keyboard.println("Hello");
  Keyboard.press(KEY_LEFT_GUI);
  Keyboard.press('r');
  Keyboard.releaseAll();
}
```

v3では：

```cpp
#include <EspUsbDevice.h>
EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);

void setup() {
  keyboard.setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_US);  // Hostのlayout
  EspUsbDeviceConfig config;
  config.pid = 0x4001;
  config.product = "My Keyboard";
  device.begin(config);
}
void loop() {
  if (!device.ready()) { delay(10); return; }
  keyboard.write("Hello\n");
  keyboard.tapUsage(ESP_USB_HID_KEY_R, ESP_USB_DEVICE_MOD_LEFT_GUI);
  delay(1000);
}
```

| Core | v3 | 注意 |
| --- | --- | --- |
| `Keyboard.print()` / `println()` / `write(c)` | `keyboard.write("text")`、`keyboard.tapKey(c)` | layoutに従う。`write()`はlayoutで打てない最初の文字で`false`を返す |
| `Keyboard.begin(layout)` | `keyboard.setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_*)` | **Host**が使うlayoutに合わせる |
| `Keyboard.press(KEY_LEFT_GUI)`の後に`press('r')` | `keyboard.pressUsage(ESP_USB_HID_KEY_R, ESP_USB_DEVICE_MOD_LEFT_GUI)`または`tapUsage(...)` | HIDのusage IDと修飾キーのbitが基本のAPI |
| `Keyboard.press(c)` / `release(c)` | `keyboard.pressKey(c)` / `releaseKey(c)` | `releaseKey(c)`は、その文字を打つためにlayoutが使う修飾キーも離す。usage IDなら`releaseUsage(usage)` |
| 複数の`press()`で同時に押したまま | `pressUsage()` / `pressKey()` | Coreと同じく、押すたびにキーと修飾キーを足し込む。NKROなしでは修飾キー以外を6つまで。7つ目は`ESP_ERR_INVALID_SIZE`で、状態は変わらない。もっと押すには`enableNkro()` |
| `Keyboard.releaseAll()` | `keyboard.releaseAll()` | 修飾キーも離す |
| `Keyboard.pressRaw()` / `sendReport(KeyReport*)` | `keyboard.sendReport(EspUsbDeviceBootKeyboardReport)` | boot reportをbyteどおりに送る |
| `onEvent(ARDUINO_USB_HID_KEYBOARD_LED_EVENT)` | `keyboard.onOutputReport(...)`（`begin()`の前）、または`keyboard.ledState()`を読む | |
| - | `keyboard.enableNkro()`＋`EspUsbDeviceNkroKeyboardReport` | N-key rollover（usage 0x00〜0xDF） |
| - | `keyboard.onProtocol()` / `protocol()` | BIOS/UEFIでのboot protocolへの切替を知る |

## Mouse：`USBHIDMouse` → `EspUsbDeviceHidMouse`

| Core | v3 | 注意 |
| --- | --- | --- |
| `Mouse.move(x, y, wheel, pan)` | `mouse.move(x, y, wheel, buttons)` | **4番目の引数はpanではなくbuttonの状態。** 既定の`EspUsbDeviceHidMouse::KeepButtons`は`press()`で押したbuttonを保つので、Coreと同じく`press()`の後の`move()`でdragになる。0〜255を渡すと置き換える |
| `Mouse.click(MOUSE_LEFT)` | `mouse.click(ESP_USB_DEVICE_MOUSE_LEFT)` | `LEFT` / `RIGHT` / `MIDDLE` / `BACK` / `FORWARD` |
| `Mouse.press(b)` / `release(b)` | `mouse.press(b)` / `release(b)` / `releaseAll()` | buttonのmask |
| `Mouse.isPressed(b)` | `(mouse.buttons() & b) != 0` | |
| - | `mouse.wheel(w)`、`mouse.sendReport(EspUsbDeviceBootMouseReport)` | boot mouseの生のreport |

横scroll（pan）と絶対座標のmouse（`USBHIDAbsoluteMouse`）はありません。
どちらも`EspUsbDeviceHidCustom`に自分のreport descriptorを渡せば作れます。

## そのほかのHID

| Core | v3 | 注意 |
| --- | --- | --- |
| `USBHIDConsumerControl` | `EspUsbDeviceHidConsumerControl` | `press(usage)`、`release()`、`click(usage)`、`sendUsage(usage)`。usageの値はConsumer pageの同じID（例：`ESP_USB_DEVICE_CONSUMER_CONTROL_VOLUME_UP` = 0x00E9）。0x3FFを超える値は`ESP_ERR_INVALID_ARG` |
| `USBHIDSystemControl` | `EspUsbDeviceHidSystemControl` | `press(usage)`はGeneric Desktopのusage ID 0x81/0x82/0x83（`ESP_USB_DEVICE_SYSTEM_CONTROL_POWER_OFF`、`STANDBY`、`WAKE_HOST`）。**Coreの値1/2/3は拒否する**ので名前を使う |
| `USBHIDGamepad` | `EspUsbDeviceHidGamepad` | `send(x, y, z, rz, rx, ry, hat, buttons)`は同じ順。ほかに`sendReport()`、`releaseAll()`。hatは`ESP_USB_DEVICE_GAMEPAD_HAT_*`（0 = 中央、1〜8）。`leftStick()`/`pressButton()`などの補助はない |
| `USBHIDVendor` | `EspUsbDeviceHidVendor(device, reportSize)` | `sendInput(data, length)`。受信は`onOutputReport()`/`onFeatureReport()`（`read()`/`available()`はない）。`setFeatureReport()`でHostが読む値を設定する |
| `USBHID`＋`USBHIDDevice`の派生class | `EspUsbDeviceHidCustom(device, descriptor, length, inputReportSize)` | report descriptorのbyte列を渡す。`sendReport(data, length, reportId)`。派生classは要らない |

1つのDeviceのHID Functionはすべて、report IDを付けて**1つのHID interface**にまとめます。
keyboard＋mouse＋media keyでもIN endpointは1本です。複合のときはreport IDを自動で割り当て、`sendReport()`が付けます。

## Serial：`USBCDC` → `EspUsbDeviceCdcSerial`

移行前：

```cpp
#include "USB.h"
USBCDC USBSerial;

void setup() {
  USBSerial.begin();
  USB.begin();
}
void loop() { USBSerial.println("tick"); delay(1000); }
```

v3では：

```cpp
#include <EspUsbDevice.h>
EspUsbDevice device;
EspUsbDeviceCdcSerial usbSerial(device, "Console");

void setup() {
  EspUsbBulkStreamConfig buffers;      // 省略可。既定は512/512/512
  buffers.rxCapacity = 1024;
  usbSerial.configure(buffers);
  usbSerial.onLineState([](const EspUsbDeviceCdcLineState &s) { /* s.dtr、s.rts */ });
  device.begin();
}
void loop() {
  if (usbSerial.connected()) usbSerial.println("tick");
  delay(1000);
}
```

| Core | v3 | 注意 |
| --- | --- | --- |
| `USBSerial.begin(baud)` | （消す） | `device.begin()`が開始する。baud rateはHostが決め、`lineCoding()`で読める |
| `print` / `println` / `write` / `read` / `available` / `peek` / `readBytes` / `setTimeout` | 同じ | `EspUsbDeviceCdcSerial`はArduinoの`Stream`。`Stream&`として渡せる |
| `setRxBufferSize(n)`、TXのbuffer | `configure()`で`EspUsbBulkStreamConfig::rxCapacity` / `txCapacity` | 停止中に設定する |
| `setTxTimeoutMs(ms)` | `setTxTimeoutMs(ms)` | 既定1000 ms。`write()`と`flush()`は空きや完了をこの時間まで待つ。`0`なら送れた分だけですぐ戻る |
| `if (USBSerial)` | `usbSerial.connected()` | Hostがportを開いてDTRを立てている間true |
| `onEvent(ARDUINO_USB_CDC_LINE_CODING_EVENT)` | `onLineCoding(callback)` | `device.begin()`の前 |
| `onEvent(ARDUINO_USB_CDC_LINE_STATE_EVENT)` | `onLineState(callback)` | |
| `onEvent(ARDUINO_USB_CDC_RX_EVENT)` | `onRx(callback)` | |
| `enableReboot()`（Hostの要求でbootloaderへ再起動） | `onLineCoding()`と、`loop()`からの`device.rebootToBootloader()` | v3は自分では再起動しない。[USB経由のファームウェア更新](ota-over-usb.ja.md#24-sketchからboot-modeに入る)を参照 |
| `USB CDC On Boot`（`Serial`をUSBに出す） | - | `Serial`は別のまま。CDCのFunctionはsketchのdata用の口 |

CDCのportを複数にするには、`EspUsbDeviceCdcSerial`を複数作ります。Windowsで区別できるよう、それぞれに名前を付けます。
1つにつきIN endpointを2本使います。

## Mass storage：`USBMSC` → `EspUsbDeviceMsc`

callbackの形は同じです。呼ぶ時点が`device.begin()`の前に移ります。

```cpp
EspUsbDevice device;
EspUsbDeviceMsc msc(device);

void setup() {
  msc.vendorID("ESP32");
  msc.productID("Disk");
  msc.productRevision("1.0");
  msc.onRead([](uint32_t lba, uint32_t offset, void *buffer, uint32_t size) -> int32_t { /* ... */ return size; });
  msc.onWrite([](uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size) -> int32_t { /* ... */ return size; });
  msc.begin(blockCount, 512);   // 容量の設定だけ。先にonRead()が要る
  msc.mediaPresent(true);
  device.begin();               // MSCを含めてUSBを開始する
}
```

| Core | v3 | 注意 |
| --- | --- | --- |
| `vendorID()` / `productID()` / `productRevision()` | 同じ名前 | `esp_err_t`を返す。停止中だけ |
| `onRead()` / `onWrite()` / `onStartStop()` | 同じ名前、同じ引数 | `device.begin()`の前。callbackは要求された量を同期で全部返す |
| `mediaPresent(bool)` | 同じ | 最初は媒体なし。Hostが構成済みのときに挿し直すと、UNIT ATTENTIONを1回返す |
| `begin(blockCount, blockSize)` | 同じ名前 | 容量を設定するだけ。USBは`device.begin()`が開始する |
| `isWritable(bool)` | 同じ | 既定は書込み可 |
| `end()` | `device.end()` | |
| - | `configure()`で`EspUsbMscConfig::transferBytes` | stagingの大きさ。既定512。bulkのMPS（HSでは512）の倍数が必要 |

用意されたbackendを使えば、多くのsketchはcallbackを書かずに済みます。
[`EspUsbDeviceMscRamDisk`](../examples/MSC/)（生のblock）、[`EspUsbDeviceMscFatRamDisk`](../examples/MSCFatRamDisk/)（fileを置けるFAT12のimage）、
[`EspUsbDeviceMscSdCard`](../examples/MSCSdCard/)（SD card）です。どれも`device.begin()`の前に`disk.attach(msc)`でつなぎます。
RAM diskは媒体なしで始まるので、`msc.mediaPresent(true)`を呼びます。

### `FirmwareMSC` / *USB MSC On Boot* → `EspUsbDeviceMscFirmwareDisk`

Coreのfirmware driveは`EspUsbDeviceMscFirmwareDisk`になります。data領域が予備のOTA partitionであるFAT volumeで、
生の`.bin`と`.uf2`を受け付けます。[USB経由のファームウェア更新の3.6](ota-over-usb.ja.md#36-firmware-drive)と
[`FirmwareMSC`](../examples/FirmwareMSC/)を参照してください。

## MIDI：`USBMIDI` → `EspUsbDeviceMidi`

```cpp
EspUsbDeviceMidi midi(device);            // または (device, inCables, outCables)

midi.noteOn(0, 60, 96);                   // channel, note, velocity[, cable]
midi.noteOff(0, 60, 0);
midi.controlChange(0, 74, 32);

EspUsbDeviceMidiPacket packet;
while (midi.readPacket(packet)) { /* 4 byteのUSB-MIDI event */ }
```

| Core | v3 |
| --- | --- |
| `MIDI.noteOn(note, velocity, channel)`、channelは1〜16 | `midi.noteOn(channel, note, velocity, cable)`、**channelは先頭で0〜15** |
| `noteOff`、`controlChange`、`programChange`、`polyPressure`、`channelPressure` | 同じ名前。channelが先頭で0始まり |
| `pitchBend(value, channel)` | `pitchBend(channel, value)`。valueは0〜16383（8192が中央） |
| `readPacket(midiEventPacket_t*)` / `writePacket()` | `readPacket(EspUsbDeviceMidiPacket&)` / `writePacket()` |

**引数の順に注意してください。** 機械的に置き換えるとcompileは通り、正しく動きません。
Coreの`noteOn(60, 96, 1)`はchannel 60と読まれます。範囲外なので、`false`（`ESP_ERR_INVALID_ARG`）を返し、何も送りません。
値は丸めません。たまたま範囲内の引数なら、違う音が出ます。Coreの1始まりのchannelから1を引くことも必要です。
複数cableのinterface（各向き16まで）、callbackでの受信（`onPacket()`）、packet単位のbuffer設定（`EspUsbMidiConfig`）はCoreにはありません。
[`MIDIInterface`](../examples/MIDIInterface/)を参照してください。

## Vendor：`USBVendor` → `EspUsbDeviceVendor`

| Core | v3 | 注意 |
| --- | --- | --- |
| `Vendor.write()` / `read()` / `available()` | 同じ名前（bulk IN/OUT） | `EspUsbDeviceVendor`はArduinoの`Print`。bufferは`configure(EspUsbVendorConfig)`で |
| `Vendor.onRequest(handler)` | `vendor.onControlRequest(callback)` | `device.begin()`の前。callbackは`bool`を返す |
| `Vendor.sendResponse(rhport, request, data, len)` | `vendor.sendControlResponse(request, data, len)` | |
| `Vendor.onEvent(ARDUINO_USB_VENDOR_DATA_EVENT)` | `onRx(callback)` | |
| `USB.webUSB(true)` / `webUSBURL(url)` | `config.webusbEnabled` / `config.webusbUrl` | |

ライブラリはVendorのinterfaceごとにMicrosoft OS 2.0 descriptorを自動で出すので、WindowsはINFやZadigなしでWinUSBを付けられます
（v3ではWindowsで未確認）。`config.webusbEnabled`はWebUSBの能力を足すだけです。
streamの書込みは、既定では短いpacketで終わりません。Host側で長さを決めて読むか、`streamTermination`を設定します。
copyしない転送には`writeDirect()`/`readDirect()`を使えます。[`USBVendor`](../examples/USBVendor/)を参照してください。

## DFU：*USB DFU On Boot* → `EspUsbDeviceDfu`

`EspUsbDeviceDfu(device, EspUsbDeviceDfuMode::Runtime)`は`dfu-util -e`（`DFU_DETACH`）に答え、既定ではROMのloaderへ再起動します。
`EspUsbDeviceDfuMode::Download`（既定）は`dfu-util -D firmware.bin`を受け、予備のOTA partitionへ書きます。
どちらもEP0だけを使います。[USB経由のファームウェア更新](ota-over-usb.ja.md)を参照してください。

## EspUsbDeviceにだけあるもの

- **USB Audio**（既定はUAC1、UAC2も選べる）：playback、capture、両方向のstream。
- **USB Video**（UVCのcamera、MJPEGかYUY2）。
- **CDC-NCMのnetwork device**（`EspUsbDeviceNet`）。esp_netif/lwIPとDHCPも使える。
- **CCIDのsmart card reader**（`EspUsbDeviceCcid`）。
- **applicationでのDFU**と、UF2を受け付けるfirmware drive。
- NKRO keyboard、生のreportのAPI、boot protocolの観察。
- **ESP32-P4のHigh Speed**（480 Mbps）。`config.controller`で選ぶ。
- 実行時のbuffer設定、再buildなしの`end()`と再開始、copyしないbulk転送。
- Hostなしでのdescriptorの確認（`EspUsbDeviceStartMode::DescriptorsOnly`、
  [`EspUsbDeviceDescriptorDump`](../examples/Info/EspUsbDeviceDescriptorDump/)）。

## ないもの

- `USBHIDAbsoluteMouse`とmouseのpan：`EspUsbDeviceHidCustom`で作る。
- gamepadの補助関数（`leftStick()`、`pressButton()`など）：`send()`か`sendReport()`を使う。
- USBのconsole（`USB CDC On Boot`、OTGの口での`HWCDC`）：logはUARTかUSB Serial/JTAGに出す。
- CDCの口からの自動のbootloader再起動：`onLineCoding()`と`rebootToBootloader()`で作る。
- device全体のevent callback（`USB.onEvent`）：`ready()`と`suspended()`を調べる。

移行の後で動きがおかしいときは[トラブルシューティング](troubleshooting.ja.md)を見てください。
特に[descriptorの記憶の項](troubleshooting.ja.md#変更したのにhostに古いdeviceが見える)です。
CoreのstackのVID/PIDのままのboardは、移行の直後にWindowsを混乱させます。
