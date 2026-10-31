# USB経由のファームウェア更新

> English: [ota-over-usb.md](ota-over-usb.md)

ESP32のUSB deviceで「OTA」と呼ばれるものには、まったく別の2つがあります。失敗の仕方も違います。

- **経路A：chipをROMへ渡す。** applicationは止まります。ROMのbootloaderがUSBの口を受け持ち、
  `esptool`（chipによっては`dfu-util`）がflashを書き換えます。いわゆる「boot mode」です。
  loaderはmask ROMにあるので、書込みの失敗で壊れることはありません。
- **経路B：sketchが次のapplicationを書く。** sketchはUSBの身元を保ったまま動き続け、
  持っているUSB Functionでimageを受け取り、もう一方のOTA partitionへ書きます。
  byte列が届いた後はUSBに固有の処理はなく、Wi-Fiと同じ`esp_ota`です。

経路Aは復旧と工場での書込み用、経路Bは製品の更新用です。出荷する機器には普通は両方が要ります。
問題は、経路Bが動いている状態から経路Aへどう移るかで、それが[2.4](#24-sketchからboot-modeに入る)です。

## 目次

1. [このライブラリが受け持つもの](#1-このライブラリが受け持つもの)
2. [経路A：chipをROMへ渡す](#2-経路achipをromへ渡す)
3. [経路B：sketchが次のapplicationを書く](#3-経路bsketchが次のapplicationを書く)
4. [経路の比較](#4-経路の比較)
5. [ライブラリの機能か、sketchのコードか](#5-ライブラリの機能かsketchのコードか)
6. [実機での確認範囲](#6-実機での確認範囲)
7. [対象外：bootloaderとしてのUF2](#7-対象外bootloaderとしてのuf2)

---

## 1. このライブラリが受け持つもの

EspUsbDeviceが受け持つのはUSBの転送です。descriptor、endpoint、byte列を運ぶclass。flashは受け持ちません。
次の4つはライブラリにあります。製品ごとに選ぶものではなく、正しくなければならない部分だからです。

| 部品 | 種類 | 役割 |
| --- | --- | --- |
| `EspUsbDevice::rebootToBootloader()` | deviceの関数 | 動いているsketchからROMのdownload loaderへ入る（[2.4](#24-sketchからboot-modeに入る)） |
| `EspUsbDeviceFirmwareUpdate` | helper（Functionではない） | 予備のOTA partitionへimageを書き、検証し、起動先を切り替える（[3.2](#32-書く側はいつも同じ)） |
| `EspUsbDeviceDfu` | Function | USB DFU 1.1。RuntimeとDownloadの2つのmode（[3.5](#35-dfu-function)） |
| `EspUsbDeviceMscFirmwareDisk` | MSCのhelper | data領域がOTA partitionそのものであるdrive（[3.6](#36-firmware-drive)） |

ほかの経路は、既存のFunction（`EspUsbDeviceCdcSerial`、`EspUsbDeviceVendor`、`EspUsbDeviceNet`）と
`EspUsbDeviceFirmwareUpdate`の組合せで作ります。`Firmware*`のexampleがそれぞれを示します。

---

## 2. 経路A：chipをROMへ渡す

### 2.1 boot modeとは

reset時にROMはstrapping pinを読みます。High（既定）ならflashのapplicationを起動します。
LowならROMのdownload loaderにとどまり、Hostを待ちます。loaderはesptoolのprotocolをUARTで話し、
このライブラリが対応するchipではUSBでも話します。
firmwareではないので、flashの書込みの失敗で壊れません。だからこの文書のすべての経路の、最後の復旧手段になります。

### 2.2 手で入る方法

| chip | resetの間Lowにするpin | ほかの条件 | 注意 |
| --- | --- | --- | --- |
| ESP32-S2 | GPIO0 | - | |
| ESP32-S3 | GPIO0 | GPIO46を浮かすかLow | |
| ESP32-P4 | GPIO35 | **GPIO36をHigh** | GPIO35には弱い内部pull-upがあるので、buttonには強いpull-downが要る。GPIO35 = 0とGPIO36 = 0の組合せは無効 |

devkitでは、BOOTを押したままRESETを押して離し、BOOTを離します。
USB-UART bridgeのDTR/RTSがstrapping pinとENにつながったboardでは、`esptool`が自動で行います。
P4ではpinがGPIO0ではなくGPIO35で、もう1本のstrapping pinをHighにする必要があります。
P4がloaderに入らない一番多い原因です。

### 2.3 ROMが答えるUSBの口

| | ESP32-S2 | ESP32-S3 | ESP32-P4 |
| --- | --- | --- | --- |
| USB Serial/JTAG | なし | あり | あり |
| USBでのROMのserial loader | USB OTG（ROM内のCDC） | USB Serial/JTAG（`303a:1001`） | USB Serial/JTAG（`303a:1001`） |
| USB OTGでのROM DFU | あり | `USB_PHY_SEL` eFuseを焼いたときだけ | 使えないものとして扱う（chip revision v3.1以降で不具合） |
| pin | OTGのpin | GPIO19/20、共有 | boardの回路図を見る |

ROMのDFU deviceはVID `303a`と、chipごとの`00xx`のproduct IDを使います。
ESP-IDFのudev規則は`303a:00??`で合わせているので、自分の規則もそうします。

ここから分かること：

- **S3のfull speed PHYは共有です。** 1つのPHYと1組のpin（GPIO19 = D-、GPIO20 = D+）を、USB Serial/JTAGかUSB OTGのどちらかが使います。
  `device.begin()`はOTGへつなぎます。この切替は`RTC_CNTL_USB_CONF_REG`にあり、software resetでは戻りません。
  戻さないとROMのloaderは、自分が動かさないcontrollerにpadがつながったまま起動し、connectorには何も出ません。
  `rebootToBootloader()`は再起動の前にPHYをUSB Serial/JTAGへ戻します。
  そのため同じcableに`303a:1001`が出て、そのまま`esptool`を使えます。
- **S3のROM DFUにはeFuseが要ります。** ROMは起動時に`USB_PHY_SEL` eFuseでpadをつなぎ、applicationが残した設定は使いません。
  このeFuseを焼くと、そのboardはUSB Serial/JTAGを永久に失います。
  S3でDFUを使いたいなら`EspUsbDeviceDfu`（[3.5](#35-dfu-function)）を使います。eFuseは関係しません。
- **P4はUSB Serial/JTAGで書き込みます。** EspressifはP4 v3.1以降でROM DFUのdownloadに不具合があるとしています。
  USB Serial/JTAGは、専用のPHYを持つHSのOTG connectorとは別の口です。
  そのためP4のHS deviceでは「同じcableがloaderとして戻る」ことはありません。P4のFS PHYはS3と同じくUSB Serial/JTAGと共有です。

### 2.4 sketchからboot modeに入る

```cpp
device.rebootToBootloader();   // S2/S3/P4では戻らない
```

USBから切り離してHostに切断を記録させ、chipのdownload-boot flagを立てて再起動します。
ROMはstrapping pinに加えてこのflagも見るので、BOOTを押したときと同じくloaderに入ります。
ROMは通過時にflagを消します。`esptool`で書いてresetした後は、applicationが普通に起動します。

ライブラリの関数にしてあるのは、registerがchipごとに違うからです。
S2/S3ではflagは`RTC_CNTL_OPTION1_REG`の唯一のbitなので、register全体を書いても安全です。
P4では`LP_SYSTEM_REG_SYS_CTRL_REG`のbit 2で、同じregisterにsoftware resetのbit、`DIG_FIB`、`IO_MUX_RESET_DISABLE`があります。
書き込むのではなく、bitを立てる必要があります。S3の手順をP4へ写すと、これらのfieldを壊します。

使うときの決まりは3つです。

- **USBのcallbackではなく、sketchのtaskから呼ぶ。** callbackはUSB worker上で動きます。そこではflagを立て、`loop()`で実行します。
  callbackの中で再起動すると、Hostがまだ終えていないcontrol転送を途中で切ります。
- **意図した操作の後だけにする。** 紛れ込んだ1 byteで利用者をloaderへ落としてはいけません。Hostにはすでに2つの合図があります。
  - **1200 baudのtouch**：Arduino IDEや`arduino-cli`は、loaderを求めるときにportを1200 baudで開きます。
  - **`dfu-util -e`**：Runtime modeの`EspUsbDeviceDfu`が`DFU_DETACH`を受けます。
    handlerがなければ、要求の完了から約100 ms後に自分でROMのloaderへ再起動します。
- vendor interfaceなら専用のcontrol要求、data用の口がないdeviceならbuttonの長押しを使います。

```cpp
EspUsbDeviceCdcSerial port(device, "Console");
EspUsbDeviceDfu dfu(device, EspUsbDeviceDfuMode::Runtime, "Bootloader");
volatile bool bootloaderRequested = false;

void setup() {
  // device.begin()の前に登録する。後からだとESP_ERR_INVALID_STATEになる。
  port.onLineCoding([](const EspUsbDeviceCdcLineCoding &coding) {
    if (coding.baud == 1200) bootloaderRequested = true;   // USB worker：flagだけ
  });
  dfu.setDetachHandler([]() { bootloaderRequested = true; });
  device.begin();
}

void loop() {
  if (bootloaderRequested) device.rebootToBootloader();
}
```

`dfu.onDetach()`は、SETUPの時点で`DFU_DETACH`を受け付けるかを決めます（`false`でSTALL）。
`setDetachHandler()`は、受け付けた後の既定の再起動を置き換えます。受け付けた後は、`end()`やbus resetでも取り消されません。
[`FirmwareBootMode`](../examples/FirmwareBootMode/)を参照してください。

### 2.5 `usb_persist_restart()`は使わない

Arduino-ESP32の`usb_persist_restart(RESTART_BOOTLOADER)`は、Core自身のUSB stackのための関数で、
EspUsbDeviceのsketchではそのstackは動いていません。またP4では何もしません（実装はS2/S3にしかありません）。
`rebootToBootloader()`を使ってください。

### 2.6 ROMのserial loaderではなくROM DFUを使う（S2/S3）

```cpp
device.rebootToRomDfu();
```

ROMのpersist flag（`chip_usb_set_persist_flags(USBDC_BOOT_DFU)`）とdownload-boot flagを立てて再起動し、
ROMをUSB OTG上のDFU deviceとして起動させます。Host側は`dfu-util`か`idf.py dfu-flash`で、
`idf.py dfu`で作ったimageを使います。普通の`.bin`ではありません。

- **ESP32-S3：** `USB_PHY_SEL` eFuseを読み、焼いていなければ`false`と`ESP_ERR_NOT_SUPPORTED`を返し、**再起動しません**。
  再起動するとROMのDFUがpadのないcontrollerで動き、connectorに何も出なくなるからです。ライブラリがeFuseを焼くことはありません。
- **ESP32-S2：** ROMのUSBはUSB OTGだけなので、そのまま進みます。実機では未確認です。
- **ESP32-P4：** `false`と`ESP_ERR_NOT_SUPPORTED`を返し、何も再起動しません。

`dfu-util`を使いたいだけなら、どのchipでも同じに動く`EspUsbDeviceDfu`（[3.5](#35-dfu-function)）を使います。

### 2.7 これらを無効にするもの

- **Secure Bootかflash暗号化**を有効にすると、ROMのUSB OTG stack（serialとDFU）が使えなくなります。UARTとUSB Serial/JTAGは残ります。
- **Secure Download Mode**はDFUを無効にし、serial loaderのcommandを少数に絞ります。
- **`USB_PHY_SEL`は一度しか焼けないeFuseです。** S3で焼くと、USBでのROMのserial loaderを含め、USB Serial/JTAGがなくなります。

製品でSecure Bootを有効にするなら、USBでの経路Aは復旧手段になりません。UARTのheaderを用意します。

---

## 3. 経路B：sketchが次のapplicationを書く

### 3.1 partition

経路Bには2つのapplication partitionと`otadata`が要ります。Arduinoの既定のschemeにはあります。

```
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  ota_0,   0x10000, 0x140000,
app1,     app,  ota_1,   0x150000,0x140000,
```

1 slotあたり1.25 MBです。`huge_app`はapp partitionが1つなので、経路Bはまったく使えません。起動時に確かめます。

```cpp
if (!EspUsbDeviceFirmwareUpdate::available()) {
  // 2つ目のapp partitionがない。最後のblockではなく、今のうちに知らせる。
}
Serial.printf("%s, %u bytes\n", EspUsbDeviceFirmwareUpdate::targetLabel(),
              unsigned(EspUsbDeviceFirmwareUpdate::capacity()));
```

### 3.2 書く側はいつも同じ

どのFunctionでbyte列を運んでも、flash側は`EspUsbDeviceFirmwareUpdate`です。USBのFunctionではなく、deviceに何も登録しません。

```cpp
EspUsbDeviceFirmwareUpdate update;

update.begin(imageSize);              // 大きさが分からなければ begin()
update.write(chunk, chunkLength);     // 順番に繰り返す
if (update.end()) {                   // 検証してから起動先を切り替える
  esp_restart();
} else {
  Serial.println(update.lastErrorName());
}
```

自分で`esp_partition_write()`のloopを書くと抜けやすい点を、これが受け持ちます。

- `end()`は、起動先を切り替える前にimageのheader、長さ、checksumを確かめます。
  途中で切れたimageや壊れたimageは、次の起動ではなくここで失敗します。実行中のfirmwareは再起動まで変わりません。
- `begin()`に大きさを渡すと、`write()`はそれを超える最初のbyteで断ります。長い転送の最後まで待ちません。partitionを超える分も断ります。
- 消去は書込みの進みに合わせて行い、一度に全部は消しません。1.25 MBのpartitionの消去には数秒かかり、USBのcallbackで使う時間ではありません。
- `beginRandomAccess(imageSize)`と`writeAt(offset, ...)`は、順不同の書込みを受けます。
  順不同の書込みの周りだけを消すことはflashではできないので、imageの長さ分を最初に消します。UF2がこれを使います（[3.6](#36-firmware-drive)）。
- `abort()`は中止し、起動先は変えません。

**rollback。** `end()`は新しいimageを次の起動先にし、ライブラリはそれを自分では元に戻しません。
壊れたimageから立ち直るには、bootloaderを`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`でbuildし、
新しいfirmwareが動くと確かめた後で`EspUsbDeviceFirmwareUpdate::markValid()`を呼びます。
たとえば`device.ready()`が初めてtrueになった後で、`setup()`の中ではありません。
通常のArduinoのbuildにはrollbackがなく、`markValid()`はtrueを返すだけです。
`rollback()`は前のimageへ戻して再起動し、`cancelPendingBoot()`は再起動前に成功した`end()`を取り消します。
`markValid()`は真正性の確認ではありません。sketchが確かめない限り、更新の経路に届く人は誰でもimageを入れられます。

### 3.3 転送の手段

| 手段 | Host側 | endpoint（非control IN） | 特徴 | example |
| --- | --- | --- | --- | --- |
| **DFU**（`EspUsbDeviceDfu`） | `dfu-util` | **0**（EP0だけ） | 標準のprotocolと道具。ライブラリが全体を実装 | [`FirmwareDFU`](../examples/FirmwareDFU/) |
| **MSC**（`EspUsbDeviceMscFirmwareDisk`） | file manager（drag and drop） | 1 | 操作が一番簡単。Hostによる差も一番大きい | [`FirmwareMSC`](../examples/FirmwareMSC/) |
| **CDC ACM**（`EspUsbDeviceCdcSerial`） | serialの道具、Python script | 2 | 一番書きやすい。長さを送ってからbyte列を送る | [`FirmwareCDC`](../examples/FirmwareCDC/) |
| **Vendor / WebUSB**（`EspUsbDeviceVendor`） | PyUSB、libusb、browserのpage | 1 | control要求をcommandに、bulk OUTをdataに使う。HSで一番速い | [`FirmwareVendor`](../examples/FirmwareVendor/) |
| **CDC-NCM＋HTTP**（`EspUsbDeviceNet`） | browser | 2 | boardがUSBのnetwork adapterになり、`HTTPUpdateServer`が`/update`を出す | [`FirmwareHTTP`](../examples/FirmwareHTTP/) |

CDCとVendorのexampleにはHost用のscript（`firmware_cdc.py`、`firmware_vendor.py`）があります。
v3では各手段のbufferをその`configure()`で決めます。たとえば`FirmwareCDC`は8 KB、`FirmwareVendor`は16 KBのRX ringを使い、
sectorを消している間もHostが送り続けられるようにしています。

NCMは、Host側にbrowser以外が要らない唯一の手段です。v3ではDHCP serverが既定でoffなので、
exampleは`net.beginNetwork()`の前に`net.dhcpServer(true)`を呼びます。pageは`http://192.168.7.1/update`です。
`HTTPUpdateServer`はArduinoの`Update` libraryで同じOTA partitionへ書きます。
利用者が開発者でないならHost側のsoftwareが要らないこの手段、開発者ならcommand 1つでendpointも使わないDFUが向きます。

### 3.4 byte列を書いてはいけない場所

flashの消去は4 KBのsectorあたり数msかかり、class callbackはすべてUSB worker上で、既定ではFreeRTOSの最高priorityで動きます。
`onRx()`から直接`update.write()`を呼ぶと、その間device全体が止まります。複合deviceのほかのFunctionもです。
bulk転送ならHostはNAKを受けて遅くなるだけですが、AudioやHIDとの複合では音や動きに出ます。うまくいく形は次のとおりです。

```
USBのcallback  ->  dataはFunctionのRX ring（または自分のqueue）に残す  ->  すぐ戻る
sketchのtask   ->  read()、update.write()、繰り返す
```

CDCやVendorのstreamなら、`loop()`でringを読むだけで足ります。ringが満杯になるとライブラリはbulk OUTの受付を止めます。
これはUSBの流量制御で、dataは失われません。

次の2つは、意図してcallbackの中で書きます。

- **MSC**はWRITE(10)ごとに結果を同期で返す必要があるので、flashへの書込みはcallbackの中で行います。
  1回のcallbackはMSCの`transferBytes`までです。`FirmwareMSC`は4096（flashの1 sector）にして、
  1回のcallbackでの停止を消去1回と書込み1回に抑えています（4096は512の倍数なのでHSでも使えます）。
- **DFU**には、まさにこのための`bwPollTimeout`があります。deviceがHostに、次に問い合わせるまで待つ時間を伝えます（[3.5](#35-dfu-function)）。

### 3.5 DFU function

```cpp
EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);      // ほかのFunctionと一緒に使える
EspUsbDeviceDfu dfu(device, EspUsbDeviceDfuMode::Download, "Firmware");

void setup() {
  EspUsbDfuConfig config;
  config.transferBytes = 4096;                 // wTransferSize。既定1024
  if (dfu.configure(config) != ESP_OK) return;
  dfu.onProgress([](size_t written) { /* これまでに受け付けたbyte数 */ });
  dfu.onError([](EspUsbDfuStatus status) { /* 失敗の理由 */ });
  device.begin();
}
```

```sh
dfu-util -D firmware.bin
```

**endpointを使いません。** DFUはinterface descriptor、functional descriptor、control要求だけで、すべてEP0を通ります。
IN endpointの数で複合deviceが制限されるS3でも、いつでも追加できる唯一のFunctionです。

2つのmode（USB DFU 1.1）：

| mode | 答えるもの | 既定の動作 |
| --- | --- | --- |
| `Download`（既定） | download全体 | `EspUsbDeviceFirmwareUpdate`でimageを書き、検証し、再起動して切り替える |
| `Runtime` | `DFU_DETACH` | 要求の完了から約100 ms後に`rebootToBootloader()`（経路A） |

設定（`EspUsbDfuConfig`。停止中に`configure()`で渡す）：

| 項目 | 既定 | 意味 |
| --- | --- | --- |
| `transferBytes` | 1024 | 1 block（1回の`DFU_DNLOAD`）の最大。64〜65535。`wTransferSize`として宣言する |
| `transferBuffer` | `nullptr` | Download用のblock bufferを自分で渡す。`transferBytes`以上。CPUがcopyするのでDMAの境界合わせは要らない |
| `detachTimeoutMs` | 1000 | `wDetachTimeOut`。RuntimeがappDETACHにとどまる最大時間 |
| `downloadPollMs` | 1 | blockを書いている間に返す`bwPollTimeout` |
| `manifestPollMs` | 250 | imageの検証と反映の間に返す`bwPollTimeout` |
| `manifestationTolerant` | `false` | `false`：manifestの後はUSB resetを待つ（再起動するため）。`true`：dfuIDLEへ戻る |

Download modeで知っておくこと：

- **imageは動いているsketchを置き換えます。** DFUのinterfaceを持つbuildを送らないと、次の更新は経路Aになります。
- **失敗はSTALLではなくDFUのstatusで返ります。** ESPのapplicationでないfileは、最初のblockで
  `EspUsbDfuStatus::Write`（`errWRITE`、3）として断ります。検証に通らないimageは最後に`EspUsbDfuStatus::Verify`（`errVERIFY`、7）で失敗します。
  どちらも起動先を変えません。`dfu-util`が自動で送る`CLRSTATUS`でidleへ戻ります。
  `onComplete()`で`false`を返すと、検証済みのimageを断り、起動先を元に戻します。
- **blockを書く時点。** blockはdata段とstatus段、続く`GETSTATUS`が終わった後で保存します。
  flashへの書込みはUSB worker上で行い、`bwPollTimeout`（`downloadPollMs`）でHostに待ってもらいます。
- **再起動。** `restartWhenComplete(true)`（既定）では、manifestの成功から約500 ms後に再起動します。`false`ならsketchが好きな時点で再起動します。
- **保存先を自分で決める。** `setBackend()`で`download`と`manifest`のcallbackを渡すと、内蔵のOTAの代わりに使います（外部flash、別のprocessorなど）。
  このとき検証、反映、再起動はbackendの責任で、`onComplete()`と`restartWhenComplete()`は効きません。
- DFU interfaceは複数置けますが、内蔵のOTAで書けるのは同時に1つだけです。
- setterはすべて`esp_err_t`を返し、停止中だけ使えます。

### 3.6 firmware drive

`EspUsbDeviceMscFirmwareDisk`は、**data領域がOTA partitionそのもの**である小さなFAT volumeを見せます。
Hostが`.bin`をdriveへcopyすると、deviceはsectorが届くたびにflashへ書き、検証し、再起動して切り替えます。

```cpp
EspUsbDevice device;
EspUsbDeviceMsc msc(device);
static uint8_t diskStorage[16 * 1024];
EspUsbDeviceMscFirmwareDisk disk(diskStorage, sizeof(diskStorage));

void setup() {
  if (!disk.begin("ESPUSB")) return;          // OTA partitionがない、または領域が小さい
  disk.addTextFile("README.TXT", "Copy a firmware .bin here.\r\n");
  disk.onError([](esp_err_t error) { /* ... */ });

  EspUsbMscConfig mscConfig;
  mscConfig.transferBytes = 4096;             // flashの1 sector。FSでもHSでも有効
  if (msc.configure(mscConfig) != ESP_OK) return;
  if (!disk.attach(msc)) return;              // device.begin()の前
  device.begin();
}
```

**imageはRAMに置きません。** `diskStorage`に入るのは、boot sector、FAT、root directory、作業領域で、数KBです。
それより後ろのsectorはpartitionそのもので、`esp_partition_read()`で読み、`EspUsbDeviceFirmwareUpdate`で書きます。
だから320 KBのRAMのboardが1.25 MBのimageを受け取れます。

`attach()`は、RAM diskのhelperと違い、媒体ありと書込み可を設定します。
`begin()`と`addTextFile()`は`attach()`の前に、これらすべては`device.begin()`の前に呼びます。

このclassが決めていることと、その理由：

- **clusterの大きさ。** FAT12が扱えるclusterは4084個なので、`begin()`はpartition全体を覆える最小のclusterを4 KBから選びます。
  4 KBはflashのsectorと同じなので、clusterの境界が消去の境界になります。
- **firmwareとみなすもの。** firmware領域への書込みで、最初のbyteがESPのimageのmagic `0xE9`なら更新を始めます。
  それ以外は捨てます。誤ってcopyされたfileや、作業領域からあふれたHostのmetadataです。
- **終わりの判定。** 先に来たほうで決めます。fileのdirectory entryが示す長さにbyte数が届いたとき、またはHostがdriveを取り出したとき。
- **生の`.bin`は昇順で届く必要があります。** 生のimageは、自分のbyteがどこに入るかを持っていません。
  後ろへ戻る書込みや穴を空ける書込みは断り、`onError()`に`ESP_ERR_INVALID_STATE`を渡して更新を中止します。
  途中まで書いて完了したように見える状態にはしません。

**`.uf2`ならこの制限はありません。** UF2の512 byteのblockは、それぞれ書込み先のaddress、block番号、総数を持ちます。
driveはどちらの形式も受け、最初に届いたbyteで判断します。

| | 生の`.bin` | `.uf2` |
| --- | --- | --- |
| 書込みの順 | 昇順だけ | 順不同 |
| Hostのmetadata | `0xE9`の判定で無視 | blockのmagicで拒否 |
| 長さ | FATのdirectory entryから | blockのheaderから |
| 完了 | byte数が長さに届くか、取り出し | すべてのblock番号がそろったとき |
| 同じblockを2回書く | 進みと区別できない | 1回と数える |
| 別のchip用のimage | 受け取り、後で検証に失敗 | 最初のblockで拒否 |

`EspUsbDeviceMscFirmwareDisk::uf2FamilyId()`は、このbuildが受け付けるfamily IDを返します。
`uf2conv.py --family <id> --base 0x0`に渡します。Arduinoは`.uf2`を作らないので、この変換が手間になります。

**作業領域の大きさは意識して決めます。** `System Volume Information`、`.fseventsd`、`.Spotlight-V100`を受け止める場所で、
ここが埋まるとHostはfirmware領域の中にclusterを割り当て始めます。16 KBなら余裕があり、8 KBが最小です。

**driveかDFUか。** 操作はdriveのほうが簡単です。保証はDFUのほうが強く、費用も少なくて済みます。DFUはendpointを使わず、driveはbulkを1組使います。
`.uf2`なら正しさは同程度で、生の`.bin`ではHostが順に書くことに頼ります。
更新する人がterminalを使えるならDFU、使えないならdrive、分からないなら両方を出荷します。

### 3.7 WindowsはINFなしでdriverを付ける

DFU（interface class 0xFE）を受け持つWindows標準のdriverはありません。
そこでライブラリは、DFUとVendorの各interfaceについて、WinUSBを求めるMicrosoft OS 2.0 descriptorを、設定なしで出します。

- **対象のinterfaceが1つ：平ら（flat）。** compatible IDをsetのheader直下に置きます。
  function subsetはusbccgp.sys経由でしか解決されず、Windowsはそれを複合deviceにしか読み込みません。
- **2つ以上：interfaceごとにfunction subsetを1つ。**
- DFUのinterfaceにはcompatible IDだけを付け、`DeviceInterfaceGUIDs`は付けません。libusb（つまり`dfu-util`）はこれなしでWinUSBのdeviceを見つけます。
- BOS descriptorがあるときは`bcdUSB`を0x0201にします。HostがBOSを読みに来る基準の値です。

これらの規則は、同じdescriptor setを作るv2でWindows 11を使って実測したものです。v3はWindowsでは未確認です。
LinuxとmacOSはclassでdriverを付けるかlibusbがinterfaceを直接取るので、このsetは要りません。

---

## 4. 経路の比較

| 経路 | chip | boot mode | Hostの道具 | 動かないimageを入れうるか | API |
| --- | --- | --- | --- | --- | --- |
| USBでのROMのserial loader | S2（OTG）、S3/P4（USB Serial/JTAG） | 要る | `esptool` | いいえ | `rebootToBootloader()`（[2.4](#24-sketchからboot-modeに入る)） |
| USB OTGでのROM DFU | S2。S3は`USB_PHY_SEL`を焼いたときだけ。P4は不可 | 要る | `dfu-util` | いいえ | `rebootToRomDfu()`（[2.6](#26-romのserial-loaderではなくrom-dfuを使うs2s3)） |
| applicationのDFU | すべて | 不要 | `dfu-util` | rollbackなしなら、はい | `EspUsbDeviceDfu`（[3.5](#35-dfu-function)） |
| firmware drive（`.bin`/`.uf2`） | すべて | 不要 | file manager | rollbackなしなら、はい | `EspUsbDeviceMscFirmwareDisk`（[3.6](#36-firmware-drive)） |
| CDC ACM | すべて | 不要 | serialの道具 | rollbackなしなら、はい | `EspUsbDeviceCdcSerial`＋`EspUsbDeviceFirmwareUpdate` |
| Vendor / WebUSB | すべて | 不要 | PyUSB、browser | rollbackなしなら、はい | `EspUsbDeviceVendor`＋`EspUsbDeviceFirmwareUpdate` |
| CDC-NCM＋HTTP | すべて | 不要 | browser | rollbackなしなら、はい | `EspUsbDeviceNet`＋`HTTPUpdateServer` |

applicationの経路では、動かないimageを入れてしまうことがあります。そのときも経路Aは残ります。ROMのloaderは上書きできないからです。

---

## 5. ライブラリの機能か、sketchのコードか

分け方：**descriptor、protocolの状態、chipのregisterのように正しくなければならないものはライブラリ、方針はsketch。**

**ライブラリにあるもの**

- DFUのclass：descriptor、DFU 1.1の状態遷移とcontrol要求。sketchで手作りすべきものではありません。
- `rebootToBootloader()`：chipごとのregisterの知識。ないと、S3の解説をP4へ誤って写すことになります。
- `EspUsbDeviceFirmwareUpdate`：大きさの上限、順次消去、検証してから切り替える決まりを、全経路で共有します。
- firmware driveのFAT層：geometryの誤りは、特定のHostがmountするまで見えません。

**sketchに書くもの**

- いつboot modeに入るか：1200 baudのtouch、button、vendorのcommand。[`FirmwareBootMode`](../examples/FirmwareBootMode/)に3つの例があります。
- Host側：Pythonのuploader、WebUSBのpage、HTMLのform。
- vendor独自のupdaterのwire protocol（区切り、checksum、応答）。
  DFUをライブラリに入れてvendorのprotocolを入れない理由はこれです。DFUは既存の規格なので、実装してもライブラリ独自の約束を増やしません。
- rollbackの方針、imageの真正性の確認、進み具合の表示。

---

## 6. 実機での確認範囲

| 項目 | v3での状態 |
| --- | --- |
| DFUのprotocol（64〜65535 byteのblock、GETSTATUSとblockの順、CLRSTATUS、ABORT、manifest、複数interface）をRAMのbackendで | S3 ↔ S3、P4のFS loopback、P4 ↔ P4XのHS直結で確認 |
| DFUの`DFU_DETACH`の時間と拒否 | P4X → Linux PC（HS）でpyusbを使って確認 |
| MSCのBOTと媒体の変更をwireの段階で | P4X → Linux PC（HS）でpyusbを使って確認（OSのmountはしていない） |
| 内蔵のOTAでのDFU Downloadと、新しいimageへの再起動 | v3では未確認 |
| OSがmountしたfirmware drive、生の`.bin`と`.uf2` | v3では未確認 |
| `rebootToBootloader()`、`rebootToRomDfu()` | v3では未確認 |
| `FirmwareCDC`、`FirmwareVendor`、`FirmwareHTTP`の通し | v3では未確認 |
| `dfu-util`、Windows、macOS | v3では未確認 |
| ESP32-S2 | compileのみ |

`EspUsbDeviceFirmwareUpdate`、firmware driveのFATとUF2の処理、`rebootToBootloader()`はv2と同じコードで、v2では実機で確認しています。
ESP32-P4のHSでのDFU download（385 KBを2.7秒、その後新しいimageで再起動）、逆順に書いたUF2 block、
ESP32-S3とESP32-P4での`rebootToBootloader()`です。v3ではその下のUSB転送（MSC、DFU、controllerのdriver）が新しくなっています。

---

## 7. 対象外：bootloaderとしてのUF2

[TinyUF2](https://github.com/adafruit/tinyuf2)は2段目のbootloaderを、UF2のdriveを見せるものに置き換えます。
Espressifの[`esp_tinyuf2`](https://docs.espressif.com/projects/esp-iot-solution/en/latest/usb/usb_device/esp_tinyuf2.html)は、
これをapplication側の版と一緒にESP-IDF向けにまとめています。application側の版は[3.6](#36-firmware-drive)にあたります。
bootloaderの版は対象外です。bootloaderを置き換えること、Arduino libraryではなくESP-IDFのcomponentであること、
そしてその利点（applicationが壊れていても動く）は、経路Aがmask ROMですでに提供しているからです。

---

## 関連文書

- [USBデバイス開発ガイド](usb-device-guide.ja.md)
- [USBデバイス開発ガイド（応用）](usb-device-advanced.ja.md)
- [トラブルシューティング](troubleshooting.ja.md)
- [DFU class](classes/dfu.ja.md)、[MSC class](classes/msc.ja.md)
- example：[`FirmwareDFU`](../examples/FirmwareDFU/)、[`FirmwareMSC`](../examples/FirmwareMSC/)、
  [`FirmwareCDC`](../examples/FirmwareCDC/)、[`FirmwareVendor`](../examples/FirmwareVendor/)、
  [`FirmwareHTTP`](../examples/FirmwareHTTP/)、[`FirmwareBootMode`](../examples/FirmwareBootMode/)
- [USB DFU 1.1](https://www.usb.org/sites/default/files/DFU_1.1.pdf)
- [ESP-IDF: Device Firmware Upgrade via USB](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/dfu.html)
- [esptool: Boot Mode Selection (ESP32-S3)](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/advanced-topics/boot-mode-selection.html) / [(ESP32-P4)](https://docs.espressif.com/projects/esptool/en/latest/esp32p4/advanced-topics/boot-mode-selection.html)
- [ESP-IoT-Solution: USB-OTG peripheral introduction](https://docs.espressif.com/projects/esp-iot-solution/en/latest/usb/usb_overview/usb_otg.html)（P4 v3.1のROM DFUの不具合）
