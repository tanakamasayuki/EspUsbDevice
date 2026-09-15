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
- **`EspUsbDeviceFirmwareUpdate`** — どの経路にも必要なOTA partition writer。
  [3.2](#32-書き込み側はどの経路でも同じ)。

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
| **MSC** (`EspUsbDeviceMsc` + `EspUsbDeviceMscFatRamDisk`) | drag and drop | 1 (bulk duplex) | UXは最良、正しく作るのは最難。[6.1](#61-msc経由のdrag-and-drop)参照。 |
| **DFU** (`EspUsbDeviceDfu`) | `dfu-util` | **0** | 標準のhost toolと標準のprotocolがEP0上で動きます。経路全体をライブラリが実装し、スケッチはcallbackを渡すだけ。[3.5](#35-dfu-function)。 |

このうち2つはexampleとして同梱しています。
[`FirmwareDFU`](../examples/FirmwareDFU/) と
[`FirmwareHTTP`](../examples/FirmwareHTTP/) です。

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
| MSC経由の自力OTA (drag and drop) | 全部 | 不要 | ファイルマネージャ | rollback無しならしうる | ⚠ classはあるがOTAの繋ぎが無い — [6.1](#61-msc経由のdrag-and-drop) |
| UF2 | S2 / S3 (TinyUF2) | 場合による | drag and drop | bootloader版はしない | ❌ — 外部プロジェクト、[6.2](#62-uf2) |

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
- ❌ RAM disk上にファイルが現れたことを検知するFAT層。filesystemの解析であり、繊細で、
  formatは既に `EspUsbDeviceMscFatRamDisk` が持っています。
  [6.1](#61-msc経由のdrag-and-drop)。

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

`EspUsbDeviceDfu`、`EspUsbDeviceFirmwareUpdate`、
`EspUsbDevice::rebootToBootloader()` はこのリストの最初の3項目で、ライブラリに
入りました。以下は残りです。残る2つは「deviceが見せるドライブにファームウェアの
ファイルが現れる」という同じ話で、実装の大半を共有します。

ここに書くものはすべて設計スケッチとコスト見積もりであって、約束ではありません。

### 6.1 MSC経由のdrag and drop

**見え方:** deviceが小さなFAT driveとして現れる。ユーザーが `firmware.bin` を
そこへ落とす。deviceが空いているOTA partitionへ書き、再起動する。

**やり方:** Arduino-ESP32自身の `FirmwareMSC` (`cores/esp32/`) が参照実装で、罠を
理解する最短経路はこれを読むことです。imageをRAMに溜めません。データ領域へ最初に
書かれたsectorの先頭にESP imageのmagic byte `0xE9` を見つけ、以降のsectorをOTA
partitionへ直接流し込み、offsetがsector境界に来るたびにflash sectorをeraseします。
それとは別にroot directory sectorへの書き込みを監視して、ファイルの実際の長さを
知ります。最後に `esp_image_verify()` を走らせ、長さを比較し、
`esp_ota_set_boot_partition()` を呼びます。

**ここでまだやっていない理由:** hostは好きなものを好きな順に書くからです。

- macOSは `.fseventsd` と `.Spotlight-V100` を、Windowsは
  `System Volume Information` を作ります。これらはデータ領域への書き込みですが
  firmwareではありません。
- directory entryはデータの前に書かれるかもしれないし、後かもしれないし、途中かも
  しれません。`FirmwareMSC` は2系統のコードと状態機械でこれを捌いており、それが
  最低ラインです。
- MSCには「ファイルが閉じられた」イベントがありません。完了はバイト数がdirectory
  entryのsizeに達したことか、ejectから推定します。ライブラリは既に
  `EspUsbDeviceMscFatRamDisk::onEject()` でejectを公開しており、こちらの方が綺麗な
  commit点です。
- RAM diskは、実際には保持しないimageのFAT metadataを置けるだけの大きさが必要です。
  `FirmwareMSC` はOTA partition sizeから幾何を計算し、0xFF4 clusterでFAT12とFAT16を
  切り替えます。

**提案する切り分け:** ライブラリ側に `EspUsbDeviceMscFirmwareDisk` (幾何、検出、
`EspUsbDeviceFirmwareUpdate` への streaming、eject時commit)、それをLEDとserial logに
繋ぐexample。書き込み側は完成済み（DFUが使うのと同じ `EspUsbDeviceFirmwareUpdate`
です）なので、残るのはFATの幾何と検出だけで、複雑度は既存の
`EspUsbDeviceMscFatRamDisk` と同程度です。そちらが自然な基底になります。

**検討に値する代案: 素の `.bin` ではなくUF2。** UF2ファイルは自己記述的な512 byte
blockの列で、各blockが自分のtarget address、block index、総数を持ちます。上の罠は
すべて消えます。各blockが行き先を持つので順不同でよく、hostのmetadataはUF2 magicを
持たないので弾かれ、完了は「N個中n個目」がデータに入っているので正確です。代償は、
ユーザーにArduinoが吐いた `.bin` ではなく `.uf2` を渡さなければならないこと。
[6.2](#62-uf2)参照。

### 6.2 UF2

[TinyUF2](https://github.com/adafruit/tinyuf2) はsecond-stage bootloaderを置き換える
S2/S3向けUF2 bootloaderで、Espressifの
[`esp_tinyuf2`](https://docs.espressif.com/projects/esp-iot-solution/en/latest/usb/usb_device/esp_tinyuf2.html)
はそれと、通常のapp内で動きOTA partitionを2つ要求するapplication側の版
(`usb_uf2_ota`)、さらにNVSを `.ini` に落とす機能をまとめています。

bootloader版は対象外です。bootloaderを置き換えるものであり、ESP-IDF componentであり、
ArduinoのUSB *device library* の関心事ではありません。application側の版はまさに
[6.1](#61-msc経由のdrag-and-drop)をより良いコンテナ形式でやるものです。MSC firmware
diskを作るなら、同じclassで素の `.bin` と並べてUF2 blockも受け付けるのは小さな追加で、
しかも堅牢さの本体です。

摩擦はhost側にあります。Arduinoのbuildから `.uf2` を作るには変換の一手間
(`uf2conv.py`、targetのfamily ID、base address 0x00) が要り、Arduinoはやってくれません。

なおUF2はDFUの代わりではありません。答える問いが違います。DFUはtoolを持つhost向け、
UF2はファイルマネージャしか無いhost向けです。DFUはendpointを消費しないので、
1台に両方載せられます。

## 関連ドキュメント

- [USB Device開発ガイド](usb-device-guide.ja.md) — 基礎、コネクタ、立ち上げ
- [USB Device開発ガイド (応用)](usb-device-advanced.ja.md) — callbackのcontext、endpoint予算、classの追加
- [トラブルシューティング](troubleshooting.ja.md) — 症状から引ける対処
- [examples/FirmwareDFU](../examples/FirmwareDFU/) — 動作中のスケッチを `dfu-util` で更新する
- [examples/FirmwareHTTP](../examples/FirmwareHTTP/) — USBネットワーク越しにブラウザからアップロード
- [examples/FirmwareBootMode](../examples/FirmwareBootMode/) — ROM loaderを要求する3通りの方法
- [examples/UsbNetwork](../examples/UsbNetwork/) — HTTP経路の土台になるCDC-NCM + web server
- [examples/MSCFatRamDisk](../examples/MSCFatRamDisk/) — MSC経路が土台にするFAT RAM disk
- [tests/peer/usb_dfu](../tests/peer/usb_dfu/) — ここでのDFUの主張を支える2台構成テスト
- [ESP-IDF: Device Firmware Upgrade via USB](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/dfu.html)
- [esptool: Boot Mode Selection (ESP32-S3)](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/advanced-topics/boot-mode-selection.html) / [(ESP32-P4)](https://docs.espressif.com/projects/esptool/en/latest/esp32p4/advanced-topics/boot-mode-selection.html)
- [ESP-IoT-Solution: USB-OTG peripheral introduction](https://docs.espressif.com/projects/esp-iot-solution/en/latest/usb/usb_overview/usb_otg.html) — P4 v3.1のDFU不具合
