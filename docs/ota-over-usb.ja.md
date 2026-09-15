# USB経由のファームウェア更新

> English: [ota-over-usb.md](ota-over-usb.md)

ESP32のUSB deviceで「OTA」と呼ばれるものには、まったく別の2つがあります。
壊れ方も違います。

- **経路A — chipをROMに明け渡す。** application は動作を止めます。ROM bootloader が
  USB portを取り、`esptool` や `dfu-util` がflash全体を書き換えます。「boot modeに
  入れてOTGケーブル経由で焼く」とはこれのことです。loader自体がmask ROM上にあるので、
  途中で失敗してもbrickにはなりません。
- **経路B — スケッチ自身が次のapplicationを書く。** 自分のコードは動き続け、USBの
  identityも保ったまま、既に持っているUSB functionでimageを受け取り、もう一方の
  OTA partitionへ書きます。バイトが届いた後はUSB固有の話は何もなく、Wi-Fi経由の
  OTAと同じ `esp_ota` / Arduino `Update` です。

経路Aは復旧と工場出荷の経路、経路Bは製品の経路です。出荷する機器はたいてい両方を
必要とし、面白い問題は「Bが動いている状態からどうやってAへ行くか」で、それが
[2.4節](#24-スケッチからboot-modeへ入る)です。

## 目次

1. [このライブラリが持つ責任と持たない責任](#1-このライブラリが持つ責任と持たない責任)
2. [経路A: chipをROMに明け渡す](#2-経路a-chipをromに明け渡す)
3. [経路B: スケッチが次のapplicationを書く](#3-経路b-スケッチが次のapplicationを書く)
4. [経路の比較](#4-経路の比較)
5. [ライブラリ機能かスケッチ側のコードか](#5-ライブラリ機能かスケッチ側のコードか)
6. [未実装のもの](#6-未実装のもの)

---

## 1. このライブラリが持つ責任と持たない責任

EspUsbDeviceが持つのはUSBの**転送路**です。descriptor、endpoint、バイトを運ぶclass。
flashは持ちません。

経路Bのうち2つはライブラリのものです。好みの問題ではなく、正しくなければならない
部分だからです。

- **`EspUsbDeviceDfu`** — DFU function（2形態）。標準のhost toolでdeviceを更新できます。
  [3.5](#35-dfu-function)。
- **`EspUsbDeviceMscFirmwareDisk`** — hostがファームウェアのファイルを放り込むドライブ。
  [3.6](#36-ファームウェアドライブ)。
- **`EspUsbDeviceFirmwareUpdate`** — 上の2つと、手書きのどの経路も乗るOTA partition
  writer。[3.2](#32-書き込み側はどの経路でも同じ)。

経路Bの残りは、ライブラリが既に提供しているclass
(`EspUsbDeviceCdcSerial`、`EspUsbDeviceVendor`、`EspUsbDeviceNet`、
`EspUsbDeviceMsc`) を、そのwriterの上で使って書きます。まだ無いものは
[6節](#6-未実装のもの)に挙げます。

`esp_restart()` が走った後の経路Aはこのライブラリと無関係で、portはROMのものになります。
ただし**そこへ到達する**まではライブラリの仕事です。
`EspUsbDevice::rebootToBootloader()`（[2.4](#24-スケッチからboot-modeへ入る)）が
あるのは、registerがtargetごとに違うからであり、Arduinoの定番の入り方がここでは
linkできないからです
([2.5](#25-usb_persist_restart-はこのライブラリではlinkできない))。

---

## 2. 経路A: chipをROMに明け渡す

### 2.1 boot modeとは何か

reset時にROMがstrapping pinを読みます。High (既定) なら「flashからapplicationを起動」、
Lowなら「ROMのdownload loaderに留まりhostを待つ」。download loaderはUARTで、そして
このライブラリが対象とするchipではUSBでも、esptoolのプロトコルを話します。

これはfirmwareではありません。flashの書き込み失敗で壊れることがなく、それがこのページの
他のすべての経路の背後に経路Aが控えている理由です。

### 2.2 手動で入る方法 (chip別)

| Chip | reset時にLowにするpin | 同時に必要な条件 | 備考 |
|---|---|---|---|
| ESP32-S2 | GPIO0 | — | |
| ESP32-S3 | GPIO0 | GPIO46 がfloatingまたはLow | |
| ESP32-P4 | GPIO35 | **GPIO36 がHigh** | GPIO35には約45kΩの内部pull-upがあるため、buttonには強いpull-down (GNDへ10kΩなど) が必要。GPIO36=0 と GPIO35=0 の組み合わせは無効で、動作が予測できません。 |

devkitではこれがBOOT buttonです。BOOTを押しながらRESETを叩き、BOOTを離す。
USB-UART bridgeのDTR/RTSがstrapping pinとENに配線されているboardでは `esptool` が
やってくれるので、存在に気づきません。

**P4ではpinが違います (GPIO0ではなくGPIO35)。** しかもP4にはHighでなければならない
strapping pinがもう1本あります。「S3と同じようにやったのにP4がloaderに入らない」の
最大の原因がこれです。

### 2.3 ROMがどのUSB interfaceで応答するか

既に挿さっているOTGケーブルがそのまま使えるかどうかを決めるのがここで、S3とP4が
本当に違うのもここです。

| | ESP32-S2 | ESP32-S3 | ESP32-P4 |
|---|---|---|---|
| USB-Serial-JTAG peripheral | なし | あり | あり |
| USB経由のROM serial loader | USB-OTG、ROM内のCDC-ACM | USB-Serial-JTAG (`303a:1001`) | USB-Serial-JTAG (`303a:1001`) |
| USB-OTG経由のROM DFU | あり | あり。ただし下記参照 | あり。**P4 v3.1以降は不具合あり** |
| pin | OTGのpin | GPIO19/20、共有 | boardの回路図を参照 |

ROMのDFU interfaceはVID `303a`、product IDはtarget別で `00xx` の範囲に出ます。
ESP-IDF自身のudev ruleも固定PIDではなく `303a:00??` で照合しており、こちらも
同じようにすべきです。

押さえておく価値のある帰結が3つあります。

**S3では内蔵full-speed PHYが共有されています。** PHYは1つ、pinも1組
(GPIO19=D-、GPIO20=D+)、所有者になり得るのはUSB-Serial-JTAG peripheralか
USB-OTG controllerの2つ。`EspUsbDevice::begin()` がPHYを作った時点で、スケッチは
これをOTG側へ切り替えています。**resetすると戻ります。** `USB_PHY_SEL` eFuseを
焼いていない限り、既定はUSB-Serial-JTAGだからです。つまりconnectorが1つしかない
S3 boardでは、HID deviceを運んでいたのと同じケーブルが、boot modeへのreset後には
ROMのserial loaderとして現れます。これが「1本のケーブルで焼ける」話の実体で、
eFuseもDFUも要りません。

**S3のROM DFUは厄介な方です。** PHYの既定がUSB-Serial-JTAGなので、素のresetで
出てくるのはROMの*DFU* interfaceではありません。ESP-IDFの回答は `USB_PHY_SEL` を
恒久的に焼くことですが、それはUSB-Serial-JTAGを永久に失うことを意味します。
もう1つの回答がROMのpersist flagで、これは恒久的ではありません
([2.6](#26-rom-serial-loaderではなくrom-dfuへ入る-s2s3))。

**P4ではUSB-Serial-JTAGを使ってください。** P4 v3.1以降はROM DFUのdownload機能に
不具合があり、書き込みにはUSB-Serial/JTAGを使うこと、というのがEspressif自身の
案内です。P4のROM DFUは使えないものとして、USB-Serial-JTAG portを前提に設計して
ください。なお、これはdeviceが載っているであろう高速OTG connectorとは**別のport**
です。P4のhigh-speed controllerは専用PHYを持つため、S3のような「同じケーブルが
loaderとして戻ってくる」効果はHS deviceにはありません。ただし*full-speed* port上の
P4 deviceでは効きます。こちらのPHYはS3と同じくUSB-Serial-JTAGと共有だからです
([ガイド 3.2節](usb-device-guide.ja.md#32-esp32-p4のfshs選択))。
`examples/P4FullSpeedDevice` から始める方が楽な理由がもう1つ増えたことになります。

### 2.4 スケッチからboot modeへ入る

button不要の確実な経路がこれで、呼び出し1つです。

```cpp
device.rebootToBootloader();   // 戻らない
```

USB deviceをdetachし（hostには「消えた」ではなく切断として記録されます）、target の
download-bootフラグを立てて再起動します。ROMはstrapping pinに加えてこのフラグも見るので、
BOOTを押していたのと同じようにdownload loaderへ落ちます。

スケッチに4行書くのではなくライブラリの呼び出しにしてあるのは、その4行がchipごとに
違うからです。S2/S3ではflagは `RTC_CNTL_OPTION1_REG` のbit 0で、このregisterは他に
何も持たないためregister全体への書き込みで安全です(Arduino-ESP32自身がそうしています)。
P4ではRTC controllerが無くなり、flagは `LP_SYSTEM_REG_SYS_CTRL_REG` のbit 2です。
このregisterはsoftware resetのbit、`DIG_FIB` field、`IO_MUX_RESET_DISABLE` も
抱えているので、**書き込むのではなくsetしなければなりません**。S3向けの手順をP4に
コピーすると無関係な3つのfieldを潰します。この呼び出しはそれを防ぐためにあります。

`esptool` ではなく `dfu-util` を使うhost向けには `device.rebootToRomDfu()` が同じ
役割です（[2.6](#26-rom-serial-loaderではなくrom-dfuへ入る-s2s3)）。

EspUsbDevice自身のUSB stackが動いている状態で実機確認済みです。ESP32-S3 (rev v0.2)
とESP32-P4 (rev v1.3) の両方がdownload loaderに入り、buttonにもDTR/RTS resetにも
触らずに `esptool --before no-reset chip-id` が接続しました。スケッチは
[`examples/FirmwareBootMode`](../examples/FirmwareBootMode/) です。

**このflagは残りません。** `esptool` が書き込んでhard resetした後、applicationは
通常どおり起動しました。ROMが通過時にclearします。loaderへ入り続けるboardになる
心配はありません。

この4行の周りで外してはいけない点が3つあります。

- **USB callbackからではなく、スケッチ自身のtaskから呼ぶこと。** このライブラリの
  class callbackはすべてusbd task上で動きます
  ([応用ガイド 8節](usb-device-advanced.ja.md#8-コールバックのコンテキスト))。callbackでは
  flagを立て、`loop()` から `rebootToBootloader()` を呼びます。callbackの中で再起動すると、
  hostがまだ終えていないcontrol transferを切ってしまいます。
- **意図的な操作の後ろに置くこと。** CDC portに紛れ込んだ1バイトでユーザーの作業が
  飛ぶべきではありません。hostが既に話せる作法は1200bps touchです。Arduino IDEと
  `arduino-cli` は、boardにloaderへ入るよう頼むとき、portを1200bpsで開いてDTRを
  落とします。`EspUsbDeviceCdcSerial` はその要求をそのまま渡してくれます。

```cpp
EspUsbDeviceCdcSerial serial(device);
volatile bool rebootRequested = false;

serial.onLineCoding([](const EspUsbDeviceCdcLineCoding &coding) {
  if (coding.baud == 1200) {
    rebootRequested = true;   // usbd task。flagを立てるだけ
  }
});

void loop() {
  if (rebootRequested) {
    device.rebootToBootloader();
  }
}
```

  hostが既に持っているもう1つの作法が `dfu-util -e` です。`EspUsbDeviceDfu` を
  `Runtime` modeで足すと、hostは `DFU_DETACH` で要求し、classがloaderへの再起動で
  応えます。vendorやWebUSB interfaceなら専用のcontrol requestを使います。
  data portを持たないdeviceならbuttonの長押しで十分です。要は意図的であること。

### 2.5 `usb_persist_restart()` はこのライブラリではlinkできない

Arduino-ESP32はまさにこの用途に `usb_persist_restart(RESTART_BOOTLOADER)` を用意して
おり、どのtutorialもこれを使います。**EspUsbDeviceのスケッチからは使えません。**
呼ぶと `esp32-hal-tinyusb.c` がlinkに引き込まれ、そのfileはこのライブラリが定義して
いるのと同じTinyUSB callbackを2つ定義しています。

```
esp32-hal-tinyusb.c:405: multiple definition of `tud_descriptor_bos_cb';
  EspUsbDevice.cpp:451: first defined here
esp32-hal-tinyusb.c:417: multiple definition of `tud_vendor_control_xfer_cb';
  EspUsbDevice.cpp:759: first defined here
```

`USB.begin()` と同じ排他性
([ガイド 3.5節](usb-device-guide.ja.md#35-arduino-esp32標準usbスタックとは排他))
が、実行時の衝突ではなくlink errorとして出てきているだけです。
`EspUsbDevice::rebootToBootloader()`（[2.4](#24-スケッチからboot-modeへ入る)）を
使ってください。`usb_persist_restart()` がやっているのと同じことをします。差分は
core自身のstackにしか意味のない処理だけです。

もう1つ知っておくべきこと。`usb_persist_restart()` は素のArduinoスケッチでも
**P4では何もしません**。実装全体が
`#if CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32S3` の中にあります。
P4でこれを呼べと書いてある資料は間違いです。

### 2.6 ROM serial loaderではなくROM DFUへ入る (S2/S3)

`esptool` ではなく `dfu-util` を使いたい場合、S2/S3のROMはdownload boot flagに加えて
persist flagを受け付けます。これを立てると、`USB_PHY_SEL` を焼かずに、ROMがserial
loaderではなくUSB-OTG上のDFU deviceとして立ち上がります。

```cpp
device.rebootToRomDfu();   // ESP32-S2 / ESP32-S3。P4では再起動せず false
```

中身は `chip_usb_set_persist_flags(USBDC_BOOT_DFU)` と、同じdownload-bootフラグ＋
再起動です。これらはS2/S3のESP-IDF buildが公開しているROM symbolです。ただし
**end-to-endでは未検証**です。テスト環境のS3 boardはnative USB portがテスト用PCへ
配線されていないため、chipがloaderに入ることは確認済み、hostがDFU interfaceを
bindすることは未確認です。P4に相当するものはありません。P4のESP-IDF buildはROM USB
headerを一切公開しておらず、そこでのROM DFUは
[2.3](#23-romがどのusb-interfaceで応答するか)の不具合のある経路なので、この呼び出しは
`false` を返して何もしません。

host側は `dfu-util` か `idf.py dfu-flash` で、対象は `idf.py dfu` が作るDFU imageです。
素の `.bin` ではありません。

### 2.7 これらを無効化するもの

- **Secure Bootまたはflash暗号化はROMのUSB-OTG stackを無効化します。** そのport上の
  serial emulationもDFUも止まります。UARTとUSB-Serial-JTAGは残ります。
- **Secure Download ModeはDFUを完全に無効化し**、serial loaderも小さなcommand集合に
  制限されます。
- **`USB_PHY_SEL` は一方向のeFuseです。** S3でROM DFUに到達するために焼くと、
  USB-Serial-JTAGを — ROMのUSB serial loaderも含めて — 永久に失います。製品で
  この取引をする前によく考えてください。

製品でSecure Bootを有効にするなら、USB経由の経路Aは復旧手段になりません。
UARTのheaderが復旧手段です。

---

## 3. 経路B: スケッチが次のapplicationを書く

### 3.1 partition

経路Bにはapplication partitionが2つと `otadata` partitionが必要です。Arduinoの既定の
partition schemeには既にあります。

```
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  ota_0,   0x10000, 0x140000,
app1,     app,  ota_1,   0x150000,0x140000,
```

1 slotあたり1.25MB。規模感として、EspUsbDeviceのHIDスケッチが約320KB、web serverと
`HTTPUpdateServer` を積んだCDC-NCMスケッチが約540KBなので、既定schemeで余裕があります。
`huge_app` は**使えません**。application partitionが1つしかないため、経路Bはそもそも
成立しません。早めに確認してください。`esp_ota_get_next_update_partition(NULL)` が
`NULL` を返すのがその症状です。

### 3.2 書き込み側はどの経路でも同じ

どのUSB functionがバイトを運んできたかに関わらず、flash側は
`EspUsbDeviceFirmwareUpdate` です。

```cpp
EspUsbDeviceFirmwareUpdate update;

if (!EspUsbDeviceFirmwareUpdate::available()) { /* 2つ目のapp partitionが無い */ }

update.begin();                       // 長さが分かっているなら begin(imageSize)
update.write(chunk, chunkLength);     // 到着順に繰り返し
if (update.end()) {                   // 検証してから boot partition を移す
  esp_restart();
}
```

手書きの `esp_partition_write()` loopが忘れがちな3つを、これはやってくれます。

- `end()` はboot partitionを移す前にimage header・宣言された長さ・checksumを検証
  します。途中で切れたり壊れたりしたuploadは、次回起動時ではなくここで落ちます。
- `write()` はpartitionの末尾を1バイトでも超えた時点で拒否します。送り続けるhostが
  長いuploadの最後で気づくのではなく、その場で止まります。
- `begin()` はsequential eraseでpartitionを開くので、flashは書き込みの進行に合わせて
  eraseされます。先頭でpartition全体を要求すると数秒かかり、USB callbackはそれを
  費やす場所として不適切です。

`available()` / `capacity()` / `targetLabel()` は、uploadを始める前にそもそも成立
するかを答えます。application partitionが1つだけのschemeには新しいimageの置き場所が
無く、それは最後のblockではなく起動時に言うべきことです。

不正なimageを踏んでも生き残らせたいなら、新firmwareが自分の正しさを示した後
（列挙が済んだ後であって `setup()` ではありません）に
`EspUsbDeviceFirmwareUpdate::markValid()` を呼び、bootloaderを
`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` 付きでビルドします。この組み合わせが無いと、
ゴミとして起動する新imageからの復旧は経路Aだけになります。戻る手段は `rollback()` と
`cancelPendingBoot()` です。

### 3.3 今日そのまま使える転送路

以下の4つはすべて、ライブラリが既に提供しているclassだけで今すぐ書けます。
ライブラリの変更は要りません。

| 転送路 | host側 | endpoint消費 | 形 |
|---|---|---|---|
| **CDC-ACM** (`EspUsbDeviceCdcSerial`) | 任意のserial tool、Pythonスクリプト | 2 (notif IN + bulk duplex) | 最も単純。長さを送り、バイトを流し、chunkごとに `Update.write()`。full-speed bulkの上限は理論値1.216MB/sで、CDCのframingとchunkごとのflash書き込みでそれを大きく下回ります。300KBのimageなら十分、1MBなら遅い。 |
| **Vendor / WebUSB** (`EspUsbDeviceVendor`) | PyUSB、WinUSB、WebUSB経由のブラウザ | 1 (bulk duplex) | 専用updaterに最も合います。control requestがbulk dataと並行した綺麗なcommand channel (`START`、size、`COMMIT`) になります。P4 HSでは断然最速で、ライブラリ既定の4096/4096 FIFOで片方向21.5MB/sの実測があります ([応用ガイド 6.3節](usb-device-advanced.ja.md#63-実測スループット))。 |
| **CDC-NCM + HTTP** (`EspUsbDeviceNet`) | ブラウザ | 2 (notif IN + bulk duplex) | deviceがDHCP server付きのUSB network adapterになり、`HTTPUpdateServer` が `http://192.168.7.1/update` に標準のuploadフォームを出します。driverもhost toolもライブラリのコードも不要。S3で既定app partitionの40%にbuildできることを確認済み。 |
| **MSC** (`EspUsbDeviceMscFirmwareDisk`) | drag and drop | 1 (bulk duplex) | UXは最良で、host依存の挙動が最も多い経路。[3.6](#36-ファームウェアドライブ)。 |
| **DFU** (`EspUsbDeviceDfu`) | `dfu-util` | **0** | 標準のhost toolと標準のprotocolがEP0上で動きます。経路全体をライブラリが実装し、スケッチはcallbackを渡すだけ。[3.5](#35-dfu-function)。 |

5つすべてexampleとして同梱しています。
[`FirmwareCDC`](../examples/FirmwareCDC/)、
[`FirmwareVendor`](../examples/FirmwareVendor/)、
[`FirmwareHTTP`](../examples/FirmwareHTTP/)、
[`FirmwareMSC`](../examples/FirmwareMSC/)、
[`FirmwareDFU`](../examples/FirmwareDFU/) です。

NCM経路には補足が要ります。**hostにブラウザ以外のソフトが一切要らない唯一の経路**です。
利用者が開発者でないならこれを、開発者ならDFU（1コマンド、endpoint消費ゼロ）を
選ぶとよいでしょう。

### 3.4 バイトを書いてはいけない場所

flashのeraseは遅く (4KB sectorあたり数ミリ秒)、このライブラリのclass callbackは
すべて最高優先度のusbd task上で動きます
([応用ガイド 8.1節](usb-device-advanced.ja.md#81-全部-usbd-タスクで走る))。
`onRx()` やMSCの `write10` callbackから直接 `Update.write()` を呼ぶと、その間
device全体が — composite deviceなら他のfunctionも含めて — 止まります。

bulk転送路なら通常は耐えられます (hostにはNAKが見えて減速するだけ) が、無料では
ありません。audioやHIDのcompositeでは聴こえますし見えます。成立するパターンは
こうです。

```
USB callback  ->  queue / ring bufferへ押し込む  ->  即座にreturn
スケッチのtask ->  取り出して Update.write()、繰り返し
```

MSCだけは例外で、たいてい遅延できません。`write10` は成否を同期的に返す必要があるため、
flashへの書き込みはcallbackの中で起きます。このライブラリの
`CFG_TUD_MSC_EP_BUFSIZE` は4096で、flashのsector sizeと一致するため、1 callbackあたり
erase 1回 + write 1回に停止時間を抑えられます。

### 3.5 DFU function

`EspUsbDeviceDfu` は経路Bを標準protocolと標準host toolで行うもので、このライブラリで
いちばん安いfunctionです。

```cpp
EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);
EspUsbDeviceDfu dfu(device, EspUsbDeviceDfuMode::Download, "Firmware");
```

```sh
dfu-util -D firmware.bin
```

**endpointを消費しません。** DFUはinterface descriptor、functional descriptor、
control requestの集合で、すべてEP0を通ります。endpoint予算がcomposite deviceの
上限を決めているS3（[ガイド 3.3節](usb-device-guide.ja.md#33-endpoint予算)）では、
これが「いつでも足せる唯一のfunction」であることを意味します。上のdeviceは
endpoint 2本のkeyboardとendpoint 0本のDFU interfaceで、peerテストが実際にそれを
確認しています。

2つのmode:

| Mode | 応答するもの | 既定の動作 |
|---|---|---|
| `Download` | DFU downloadの全体 | imageを書き、検証し、そのimageで再起動 |
| `Runtime` | `DFU_DETACH` のみ | `rebootToBootloader()` — ROM loader、経路A |

`Runtime` は小さい話ですが単体で価値があります。`dfu-util -e` を「任意の
EspUsbDevice deviceにboot modeへ入るよう頼む標準手段」にでき、独自protocolが
要りません（[2.4](#24-スケッチからboot-modeへ入る)）。

PC 相手に end-to-end で確認済みです。このclassを載せたESP32-P4がhigh speedでDFU
deviceとして列挙され、host toolが `wTransferSize=1024`、`bcdDFU=0x0110`、
`canDnload`、`manifestationTolerant=0` をwireから読み取り、385KBのimageが376 blockで
2.7秒（139 KiB/s）通りました。すべてEP0上で、deviceは自分のendpointを1本も持って
いません。その後deviceは `dfuMANIFEST-WAIT-RESET` を返し、boot partitionを移し、
受け取ったばかりのimageで再起動しました。

`Download` modeで知っておくべきこと:

- **送ったimageが動作中のスケッチを置き換えます。** DFU interfaceを持つビルドを
  送ってください。そうしないと次の更新は経路Aになります。
- **失敗はstallではなくDFU status codeとして返ります。** ESP applicationでない
  ファイルは最初のblockでstatus 3（`errWRITE`）、検証を通らないimageは最後に
  status 7（`errVERIFY`）です。どちらもboot partitionを動かさないので、間違った
  ファイルを送っても失うのは時間だけです。`CLRSTATUS`（`dfu-util` が自分で送ります）で
  idleに戻ります。
- **block sizeは `CFG_TUD_DFU_XFER_BUFSIZE`**（既定1024 byte）で、functional
  descriptorが宣言する `wTransferSize` でもあります。大きなimageでは `build_opt.h`
  から `-DCFG_TUD_DFU_XFER_BUFSIZE=4096` のように上げてください。descriptorも
  追随します。代償は全スケッチが抱える静的bufferで、既定をflash sector丸ごとに
  していないのはそのためです。
- **manifestation-intolerantを宣言します。** 新しいimageで再起動するからで、
  これがhostに「deviceは戻ってこず消える」と伝えます。`restartWhenComplete(false)`
  にするとスケッチが動き続け、再起動はスケッチの責任になります。
- callbackは他のclassと同じくusbd task上で動きますが、ここではflash書き込みを
  その場で行うのが*正しい*です。DFUには `bwPollTimeout` がまさにこのために
  あり、hostは推測ではなく「待て」と伝えられます
  （[3.4](#34-バイトを書いてはいけない場所)はそのfieldを持たない転送路の話です）。

### 3.6 ファームウェアドライブ

`EspUsbDeviceMscFirmwareDisk` は、**データ領域が OTA partition そのもの**である小さな
FAT ボリュームを提供します。host がドライブへ `.bin` をコピーすると、device は届いた
sector から順に flash へ書き、検証し、そのイメージで再起動します。

```cpp
EspUsbDevice device;
EspUsbDeviceMsc msc(device);
static uint8_t diskStorage[16 * 1024];
EspUsbDeviceMscFirmwareDisk disk(diskStorage, sizeof(diskStorage));

disk.begin("ESPUSB");
disk.addTextFile("README.TXT", "Copy a firmware .bin here.\r\n");
disk.attach(msc);
```

**イメージはRAMに載りません。** `diskStorage` が抱えるのは boot sector、FAT 2部、
root directory、スクラッチ領域だけで、数KBです。それより後ろの sector はすべて
partition そのもので、read は `esp_partition_read()`、write は
`EspUsbDeviceFirmwareUpdate` を通ります。RAM 320KB のボードが 1.25MB のイメージを
受け取れるのはこれが理由で、この経路が成立する唯一の条件でもあります。

classが決めていること。どれも見落としではなく判断です。

- **cluster size。** FAT12 が扱えるのは 4084 cluster なので、`begin()` は partition
  全体がその上限に収まる最小の cluster（4KB 以上）を選びます。4KB は flash の sector
  size でもあるため、cluster 境界が erase 境界になります。（Arduino-ESP32 の
  `FirmwareMSC` は 0xFF4 sector を超えると FAT16 へ切り替えます。cluster を大きくする
  のはその取引の簡単な側です。）
- **何をファームウェアと見なすか。** firmware 領域への書き込みで先頭バイトが ESP image
  magic の `0xE9` なら更新を開始します。それ以外はそこにあっても捨てます。ユーザーが
  間違って置いたファイルか、スクラッチ領域から溢れた host のメタデータであり、
  どちらも flash に届くべきではありません。
- **いつ終わりか。** 2つあり、早い方を採ります。ファイルの directory entry が示す長さに
  バイト数が達したとき、または host がドライブを eject したとき。通常は directory entry
  が先に来ます。eject は、このコードが読める directory entry を書かない host のための
  保険です。
- **素の `.bin` にできないこと。** 書き込みは昇順でなければなりません。素のイメージは
  「このバイトがどこに属するか」を何も言わないので、判断できるのは書き込み位置だけ
  です。逆戻りや穴あきは `ESP_ERR_INVALID_STATE` で拒否し、`onError()` を呼んで
  更新を中止します。中途半端に書かれた状態が「完成」に見えることはありません。

**`.uf2` を渡せば、その最後の制約が消えます。** UF2ファイルは自己記述的な512 byte
blockの列で、各blockが自分のtarget address、block index、総数を持ちます。このドライブは
それも受け付けます。形式は最初に届いたバイトから自動判別するので、同じドライブが
どちらも扱えます。コンテナで得られるもの:

| | 素の `.bin` | `.uf2` |
|---|---|---|
| 書き込み順 | 昇順のみ | **任意**。各blockが行き先を持つ |
| hostのメタデータ | `0xE9` heuristicで無視 | block magicで拒否 |
| 長さ | FATのdirectory entryから | headerから |
| 完了判定 | バイト数がその長さに達する、またはeject | 正確。全block indexを見たとき |
| 同じblockの二重書き込み | 進捗と区別できない | 1回として数える |
| 別chip向けのイメージ | 受理し、後の検証で落ちる | 最初のblockで拒否 |

後ろ2つは強調する価値があります。hostがblockを書き直すのは普通のことで、素のイメージ
ではそれがバイト数を水増しして早すぎるcommitに向かいます。UF2では「見たblock」の
bitmapがあるので数が正確です。そしてfamily IDは、ESP32-S3向けイメージをESP32-P4へ
という間違いを**flashに触れる前に**捕まえられる唯一の検査です。
`EspUsbDeviceMscFirmwareDisk::uf2FamilyId()` がこのビルドの期待値を返します。
`uf2conv.py --family` に渡すのがこの値です。

内部では、UF2のときだけwriterが
`EspUsbDeviceFirmwareUpdate::beginRandomAccess()` / `writeAt()` に切り替わります。
headerがイメージ長を教えるので、最初のblockが届く前にその分だけpartitionをeraseでき、
以降のblockはその中のどこへでも書けます。素のstreamでは最後まで長さが分からないので
これができません。

実機確認済み: UF2 block 8個を**逆順**に書いても同じイメージになること、重複blockが
カウントを進めないこと、無印ESP32のfamily IDを持つblockが何も書かれる前に拒否される
こと。

摩擦はhost側にあります。Arduinoのbuildから `.uf2` を作るには変換の一手間
(`uf2conv.py --family <id> --base 0x0`) が要り、Arduinoはやってくれません。そこが
取引です。素の `.bin` はIDEがそのまま出してくれます。

意識してサイズを決めるべきはスクラッチ領域です。`System Volume Information`、
`.fseventsd`、`.Spotlight-V100` を吸収するのがここで、埋まると host は firmware 領域の
cluster を割り当て始めます。`storage` が 16KB あればスクラッチに余裕があり、8KB が下限です。

[3.5](#35-dfu-function) との比較: UX はドライブが上、保証は DFU が少ないコストで上です。
DFU は endpoint を消費せず、ドライブは bulk 1 対を使います。`.uf2` を渡せば正しさでは
両者は近く、素の `.bin` を渡すとドライブは host の行儀を信じることになります。更新する人が
端末を使えるなら DFU、使えないならドライブ、分からないなら両方。DFU は足すのが無料です。

### 3.7 Windows が自分でdriverを当てる

DFUにはWindows標準のdriverがありません。interface class 0xFEを要求するものが何も
無いので、DFU deviceはデバイスマネージャで黄色い印が付き、ユーザーは
[Zadig](https://zadig.akeo.ie/)へ案内されることになります。それは配れるものでは
ありません。

代わりにdevice側からWinUSBを要求します。Microsoft OS 2.0 descriptor setで、
ライブラリはそれを必要なinterfaceすべて（vendorとDFU）について組み立てます。
設定は要りません。

setの形を決める規則は2つあり、どちらも推測ではなく実測で決めました。

- **interfaceが1本なら flat。** compatible IDをset headerの直下に置きます。
  configuration / function subsetは、compositeの*function*にcompatible IDを結び付ける
  ためのもので、Windowsはそれを`usbccgp.sys`経由で解決します。そして`usbccgp.sys`は
  compositeにしか載りません。単一interfaceのdeviceではsubsetsは結び付く先を持たず、
  Windowsは何も当てません。
- **2本以上なら、WinUSBが要るinterfaceごとにfunction subsetを1つずつ。** 一部しか
  指していないsubsetは、指されなかったinterfaceをdriver無しのまま残します。

ESP32-P4をWindows 11に繋いで実測しました。同じboard、同じcomposite、変えたのは
descriptor setの中身だけです。

| 構成 | DFU interface | vendor interface |
|---|---|---|
| DFU単体、Microsoft descriptor無し | `Error`、driver無し | — |
| DFU + vendor、function subsetが1つ（vendorのみ） | `problem=28` | `WINUSB` |
| **DFU単体、flat set** | **`WINUSB`** | — |
| **DFU + vendor、function subsetが2つ** | **`WINUSB`** | **`WINUSB`** |

DFU側にはcompatible IDだけを与え、`DeviceInterfaceGUIDs`は**付けていません**。
libusb（つまり`dfu-util`）はWinUSB deviceをper-function GUIDではなくUSB device
interface classで見つけますし、上の実測でbindingにGUIDが要らないことが確認できて
います。おかげでDFU単体のsetは30 byteで済みます。

`bcdUSB`はBOSを出すときだけ0x0201に上げます。これがhostがそもそもBOSを要求し始める
閾値です。0x0210にはしていません。それは実装していないUSB 2.1準拠を主張することに
なりますし、0x0201のままWindows 11でWinUSBが当たることを実測しています。

LinuxとmacOSにはこれは要りません（classで当てるか、libusbが直接claimします）。
それでも30 byteでWindowsだけが抱える「何かをインストールさせる」問題が消えるので、
出す価値があります。

---

## 4. 経路の比較

| 経路 | chip | boot mode必要 | host tool | brickしうるか | 現在のライブラリ対応 |
|---|---|---|---|---|---|
| USB経由のROM serial loader | S2 (OTG CDC)、S3 / P4 (USB-Serial-JTAG) | 要 | `esptool`、ブラウザの `esptool-js` | しない | ✅ `rebootToBootloader()` — [2.4](#24-スケッチからboot-modeへ入る) |
| USB-OTG経由のROM DFU | S2、S3。P4は不具合 | 要 | `dfu-util` | しない | ✅ `rebootToRomDfu()` — [2.6](#26-rom-serial-loaderではなくrom-dfuへ入る-s2s3) |
| **device自身が実装するDFU** | 全部 | 不要 | `dfu-util` | rollback無しならしうる | ✅ `EspUsbDeviceDfu` — [3.5](#35-dfu-function) |
| CDC経由の自力OTA | 全部 | 不要 | 任意のserial tool | rollback無しならしうる | ✅ class + `EspUsbDeviceFirmwareUpdate` |
| Vendor / WebUSB経由の自力OTA | 全部 | 不要 | PyUSB / ブラウザ | rollback無しならしうる | ✅ class + `EspUsbDeviceFirmwareUpdate` |
| CDC-NCM + HTTP経由の自力OTA | 全部 | 不要 | ブラウザ | rollback無しならしうる | ✅ [`FirmwareHTTP`](../examples/FirmwareHTTP/) |
| MSC経由の自力OTA (drag and drop) | 全部 | 不要 | ファイルマネージャ | rollback無しならしうる | ✅ `EspUsbDeviceMscFirmwareDisk` — [3.6](#36-ファームウェアドライブ) |
| MSC経由の自力OTA (UF2 コンテナ) | 全部 | 不要 | ファイルマネージャ | rollback無しならしうる | ✅ 同じ class、書き込み順は任意 — [3.6](#36-ファームウェアドライブ) |
| UF2 *bootloader* (TinyUF2) | S2 / S3 | — | drag and drop | しない | ❌ 対象外 — [6.1](#61-uf2-を-bootloader-として使う) |

---

## 5. ライブラリ機能かスケッチ側のコードか

このライブラリが他所で既に使っている境界線はこうです。**descriptorやendpoint予算で
正しくなければならないものはライブラリが持ち、方針はスケッチが持つ。**
ファームウェア更新に当てはめると:

**ライブラリに入るもの**（上3つは実装済み）

- ✅ DFU / DFU-runtimeの**class**。interface descriptor、functional descriptor、
  状態機械、control requestの集合 — まさにスケッチに手書きさせてはいけないもので、
  `EspUsbDeviceClass` が存在する理由そのものです。`EspUsbDeviceDfu`、
  [3.5](#35-dfu-function)。
- ✅ `rebootToBootloader()` を正式なAPIとして。小さいですが、target別のregister知識で
  あり、放っておけば全スケッチがS3向けtutorialからP4へ誤ってコピーします。
  [2.4](#24-スケッチからboot-modeへ入る)。
- ✅ firmware sink。「このバイトをもう一方のOTA partitionへ入れて、検証して、
  切り替えて」。`EspUsbDeviceFirmwareUpdate` として
  [3.3](#33-今日そのまま使える転送路)の全経路で共有され、size clamp、sequential erase、
  「commitの前に検証」のルールが住む場所です。
- ✅ ドライブ上にファイルが現れたことを検知するFAT層。filesystemの解析であり、繊細で、
  幾何を間違えても特定のhostがマウントするまで気づけません。
  `EspUsbDeviceMscFirmwareDisk`、[3.6](#36-ファームウェアドライブ)。

**サンプルスケッチに入るもの**

- boot modeへ**いつ**入るか。1200bps touch、button、vendor command、メニュー項目。
  これは製品の方針で、機器ごとに違います。
  [`FirmwareBootMode`](../examples/FirmwareBootMode/) が3通り示します。
- host側の相方。Pythonのuploader、WebUSBのページ、HTMLフォーム。どの意味でも
  ライブラリのコードではありません。
  [`FirmwareHTTP`](../examples/FirmwareHTTP/) はhost側がブラウザで、書くものが
  何も無い場合です。
- vendor独自updaterのwire protocol — framing、checksum、ack。サンプルは良い例を
  *1つ*示すべきで、ライブラリが押し付けるべきではありません。DFUがライブラリに入り
  vendor protocolが入らないのはこれが理由です。DFUは他人の標準なので、実装しても
  ライブラリは何も約束しません。
- rollback方針、進捗表示、更新中にdeviceが何をするか。
  [`FirmwareDFU`](../examples/FirmwareDFU/) がcallbackと `markValid()` の置き場所を
  示します。

---

## 6. 未実装のもの

このドキュメントの初版が挙げていたものは、すべてライブラリに入りました。
`EspUsbDeviceDfu`、`EspUsbDeviceMscFirmwareDisk`（素の `.bin` と UF2）、
`EspUsbDeviceFirmwareUpdate`、`EspUsbDevice::rebootToBootloader()`、そして
Zadig なしで Windows が DFU interface に WinUSB を当てるための Microsoft OS 2.0
descriptor（[3.7](#37-windows-が自分でdriverを当てる)）です。以下は意図的に対象外に
しているものだけです。

### 6.1 UF2 を bootloader として使う

[TinyUF2](https://github.com/adafruit/tinyuf2) は second-stage bootloader を
UF2 ドライブを出すものに置き換えます。Espressif の
[`esp_tinyuf2`](https://docs.espressif.com/projects/esp-iot-solution/en/latest/usb/usb_device/esp_tinyuf2.html)
はそれと application 側の版を ESP-IDF 向けにまとめたものです。

application 側の版は [3.6](#36-ファームウェアドライブ) で、これはライブラリに入りました。
bootloader 版は**対象外**です。bootloader を置き換えるものであり、Arduino のライブラリ
ではなく ESP-IDF component であり、その利点である「application が壊れていても動く」は
[経路A](#2-経路a-chipをromに明け渡す) が mask ROM から、入れるものも壊れるものも無しで
既に提供しているからです。

---

## 関連ドキュメント

- [USB Device開発ガイド](usb-device-guide.ja.md) — 基礎、コネクタ、立ち上げ
- [USB Device開発ガイド (応用)](usb-device-advanced.ja.md) — callbackのcontext、endpoint予算、classの追加
- [トラブルシューティング](troubleshooting.ja.md) — 症状から引ける対処
- [examples/FirmwareDFU](../examples/FirmwareDFU/) — 動作中のスケッチを `dfu-util` で更新する
- [examples/FirmwareHTTP](../examples/FirmwareHTTP/) — USBネットワーク越しにブラウザからアップロード
- [examples/FirmwareMSC](../examples/FirmwareMSC/) — ボードが見せるドライブにファームウェアを放り込む
- [examples/FirmwareBootMode](../examples/FirmwareBootMode/) — ROM loaderを要求する3通りの方法
- [examples/UsbNetwork](../examples/UsbNetwork/) — HTTP経路の土台になるCDC-NCM + web server
- [tests/peer/usb_dfu](../tests/peer/usb_dfu/) — ここでのDFUの主張を支える2台構成テスト
- [tests/single/msc_firmware_disk](../tests/single/msc_firmware_disk/) — ファームウェアドライブの主張を支えるテスト
- [ESP-IDF: Device Firmware Upgrade via USB](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/dfu.html)
- [esptool: Boot Mode Selection (ESP32-S3)](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/advanced-topics/boot-mode-selection.html) / [(ESP32-P4)](https://docs.espressif.com/projects/esptool/en/latest/esp32p4/advanced-topics/boot-mode-selection.html)
- [ESP-IoT-Solution: USB-OTG peripheral introduction](https://docs.espressif.com/projects/esp-iot-solution/en/latest/usb/usb_overview/usb_otg.html) — P4 v3.1のDFU不具合
