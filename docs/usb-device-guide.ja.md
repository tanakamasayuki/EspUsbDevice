# USB Device開発ガイド

> English: [usb-device-guide.md](usb-device-guide.md)

ESP32を「USB機器そのもの」にして、PCや別のHostに認識させるまでの手引きです。EspUsbDevice v3を対象にしています。

- 前半はUSB Deviceの基礎と、v3の使い方の骨組みです。
- 後半はESP32シリーズ固有の注意点と、新しい機器を立ち上げる手順です。

目標は「なぜHostが認識しないのか」を自分で切り分けられるようになることです。急ぐ場合は[立ち上げ手順](#5-立ち上げ手順)から読み、わからない用語が出たら前半に戻ってください。

- classごとの詳しい使い方は `docs/classes/` の各ガイドにあります（[1.7の表](#17-何に見せるかを選ぶ)からリンクしています）。
- 内部の仕組みや限界は[USB Device開発ガイド（上級編）](usb-device-advanced.ja.md)へ進んでください。
- v2から移る場合は[v2からの移行](migrating-from-v2.ja.md)を先に読んでください。
- ESP32をHost側にする場合は[EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost)と、その[USB Host開発ガイド](https://github.com/tanakamasayuki/EspUsbHost/blob/main/docs/usb-host-guide.ja.md)が対になります。

## 目次

1. [USB Deviceの基礎](#1-usb-deviceの基礎)
2. [EspUsbDeviceの基本の流れ](#2-espusbdeviceの基本の流れ)
3. [電源とコネクタ](#3-電源とコネクタ)
4. [ESP32シリーズ固有の注意点](#4-esp32シリーズ固有の注意点)
5. [立ち上げ手順](#5-立ち上げ手順)
6. [ホストOSから自分を観測する](#6-ホストosから自分を観測する)
7. [トラブルシューティング](#7-トラブルシューティング)
8. [ツール一覧](#8-ツール一覧)
9. [確認済みの範囲](#9-確認済みの範囲)
10. [参考資料](#10-参考資料)

---

## 1. USB Deviceの基礎

### 1.1 ホストとデバイス

USBは対等な通信ではありません。**Hostが1台あり、Deviceがそこにぶら下がる**構造です。通信は必ずHostが始めます。Deviceが自分からデータを送りつけることはできません。Hostが「データはあるか」と聞きに来たときに、初めて返せます。

このライブラリを使うとき、ESP32は**Device**側です。したがって次のことが起きます。

- **こちらから通信を始められません。** `keyboard.tapKey('a')` が実際にバスへ出るのは、Hostがそのendpointをpollingしたときです。Hostが来なければ、送信は待たされるか拒否されます。
- **自分が何者かは自己申告です。** キーボードに見えるか、シリアルポートに見えるかは、こちらが返すdescriptorで決まります。
- **Hostの都合で状態が変わります。** suspend、bus reset、configurationの解除はいつでも起きます。そのたびに転送は止まり、こちらの状態も初期化されます。

Host側の仕事が「相手のdescriptorを読んで理解すること」なら、Device側の仕事は「**Hostに理解されるdescriptorを返すこと**」です。

### 1.2 どのコネクタがデバイス側か

ESP32ボードのUSBコネクタは、形が同じでも役割が違います。

| コネクタの種類 | つながる先 | 用途 |
| --- | --- | --- |
| USB-UARTブリッジ | CP2102、CH340などのchip | 書込みとシリアルログ |
| USB Serial/JTAG | SoC内蔵の周辺回路（S3、P4） | 書込みとログ |
| **USB OTG** | SoCのOTG controller | **このライブラリが使う口** |

ESP32-P4のOTGはさらに2つ（High SpeedとFull Speed）に分かれます（[4.2](#42-esp32-p4のhsとfsの選び方)）。

シルク印刷の `USB`、`OTG`、`UART` は当てになりません。**回路図で確認してください。**

開発中は**USBコネクタが2つあるボード**を強く勧めます。ログを見る口と、Hostにつなぐ口を分けられるからです。コネクタが1つのボードでは、USB機器として動き始めた瞬間にログの口がなくなり、切り分けが極端に難しくなります。

### 1.3 速度

| 速度 | 略称 | 転送レート | このライブラリ |
| --- | --- | --- | --- |
| Low Speed | LS | 1.5 Mbps | 扱わない |
| Full Speed | FS | 12 Mbps | ESP32-S2/S3、ESP32-P4のFS controller |
| High Speed | HS | 480 Mbps | ESP32-P4のHS controllerだけ |
| SuperSpeed | SS | 5 Gbps以上 | ESP32では扱えない |

**速度はDeviceが申告します。** D+のpull-upと、HSではchirpという手順でHostに伝えます（USB 2.0 §7.1.7.5）。ただし選べるのは、そのSoCのcontrollerが持っている速度だけです。HS controllerでも、FSのHostにつなげばlinkはFSになります。

HSで動くDeviceは、「FSにつないだらどうなるか」もHostから聞かれます。これに答えるのが **Device Qualifier**（USB 2.0 §9.6.2）と **Other Speed Configuration**（§9.6.4）です。bulk endpointの最大packet長（MPS）はFSで64 byte、HSで512 byteなので（§5.8.3）、同じ構成でも速度ごとにdescriptorが要ります。HS controllerを選んだP4では、このライブラリが両方を自動で作ります。

### 1.4 列挙（enumeration）——応える側から見た流れ

Hostが機器を使えるようになるまでの手順です（USB 2.0 §9.1.2）。**どこで止まったか**が切り分けの出発点です。

1. **接続の検出**：こちらがD+をpull-upし、FS/HSのDeviceがいることを示す
2. **bus reset**：Hostがbusをresetする。HSならここでchirpを行う
3. **address割当て**：`SET_ADDRESS` に応える
4. **device descriptor**：`GET_DESCRIPTOR(DEVICE)` に18 byteを返す
5. **configuration descriptor**：構成全体を返す。ここが一番失敗しやすい
6. **string descriptor**：製造者名、製品名、serial番号
7. **`SET_CONFIGURATION`**：ここで初めてFunctionのendpointが開く
8. **Host側driverの割当て**：OSがclass codeを見てdriverを載せる

`device.ready()` が `true` になるのは7が終わった後です。

切り分けの要点：

- **1〜3で止まる**（Host側に何も出ない）→ 電気的な問題。ケーブル、コネクタ、board設定、controllerの選択。
- **4〜6で止まる**（Hostが「デバイス記述子要求の失敗」などを出す）→ descriptorの問題。
- **7まで進むがdriverが載らない**（不明なデバイスとして出る）→ class codeか、OS側driverの問題。
- **8まで進むが動かない** → class固有のprotocolの問題。

### 1.5 descriptorは自分の申告

Deviceは自分の構成を、**descriptor**という構造の列で申告します。

```
Device（VID/PID、USBのversion、EP0の大きさ）
└── Configuration（消費電流、bus給電／自己給電）
    ├── (IAD：複数のinterfaceを1つの機能にまとめる)
    ├── Interface 0（class／subclass／protocol ← 「何であるか」はここ）
    │   ├── class固有のdescriptor（HID descriptor、CDCの機能descriptorなど）
    │   ├── Endpoint 0x81（IN、interrupt、MPS 8、interval 10）
    │   └── Endpoint 0x01（OUT、…）
    └── Interface 1
        └── …
```

EspUsbDeviceでは、これを手で書く必要はありません。登録したFunctionから `begin()` の中で組み立てます。ただし**何が組み立てられたかは必ず確認してください**。[`EspUsbDeviceDescriptorDump`](../examples/Info/EspUsbDeviceDescriptorDump/README.ja.md) が全部を表示します。

押さえておく点：

- **classはDeviceではなくinterfaceに付きます。** device descriptorのclassは、IADを含む構成では `0xEF/0x02/0x01`（IADを使う複合機器）、それ以外では `0x00`（interfaceで決まる）です。どちらでも、「何であるか」はinterfaceを見ないとわかりません。IADはCDC、MIDI、Audio、Videoなど、複数のinterfaceを持つFunctionで現れます。
- **interface番号とendpoint番号はライブラリが採番します。** 手で決めるものではありません。番号は登録の順で決まります。実際の値はDescriptorDumpかHost側で確かめてください。
- **HIDのreport descriptorは別物です。** configuration descriptorには「report descriptorが何byteあるか」しか書かれません。中身はHostが別の要求で取りに来ます。HIDの入力の意味を決めているのはこちらです。
- **複数のHID Functionは1つのinterfaceにまとまります。** キーボード＋マウス＋ゲームパッドは、interfaceを増やさず、1つのHID interface上でreport IDを使って区別します。既定のIDはキーボード1、マウス2、ゲームパッド3、Consumer 4、System 5、Vendor 6です。同じ種類を重ねた場合は、空いているIDを自動で割り当てます。

### 1.6 endpointと転送の種類

endpointは通信の口です。address（`0x81` のように、bit 7が方向、下位4 bitが番号）、方向、転送の種類、MPS、polling間隔を持ちます。

| 転送の種類 | 特徴 | このライブラリでの用途 |
| --- | --- | --- |
| **Control** | EP0で行う設定用の転送。全Deviceが必ず持つ | 列挙、class要求、HIDのSET_REPORT、Vendor control、DFU |
| **Interrupt** | Hostが一定間隔でpollingする。少量・低遅延 | HID、CDCとNCMの通知、CCIDのslot通知 |
| **Bulk** | 大量のデータ。帯域の保証はないが、誤りは再送される | CDCのデータ、MSC、NCM、Vendor、MIDI、CCID、Video（bulk方式） |
| **Isochronous** | 帯域を予約する。誤りは再送されない | Audio、Video（既定） |

方向は**Hostから見た名前**です。「IN」はDevice（自分）からHostへ、「OUT」はHostからDeviceへ流れます。

Device側では、**endpointはSoCのUSB controllerが持つ本数と、controller内のFIFOを消費します**。ここがDevice開発で最初にぶつかる壁です（[4.4](#44-endpointの予算)、[4.5](#45-fifoの予算)）。

### 1.7 何に見せるかを選ぶ

Host側の開発では、相手がどう見えるかは相手が決めました。Device側では**こちらが選べます**。これがDevice開発で一番大きな設計判断です。

| 見せ方 | class | 向いている用途 | Host側のdriver | 詳しいガイド |
| --- | --- | --- | --- | --- |
| HIDキーボード／マウス／ゲームパッド | `0x03` | 入力装置、macro pad、自動操作 | OS標準。**driver不要** | [HID](classes/hid.ja.md) |
| HID（Vendor／Custom report） | `0x03` | 独自データの双方向。Windowsでもdriver不要 | OS標準（HIDAPIなどで開く） | [HID](classes/hid.ja.md) |
| CDC ACM | `0x02` | シリアルポートに見せる。既存の端末ソフトが使える | OS標準 | [CDC](classes/cdc.ja.md) |
| USB MIDI 1.0 | `0x01`/MIDI Streaming | DAW、シンセ、controller | OS標準。**driver不要** | [MIDI](classes/midi.ja.md) |
| Mass Storage（BOT） | `0x08` | ドライブに見せる。ファイルの受け渡し | OS標準 | [MSC](classes/msc.ja.md) |
| USB Audio（UAC1／UAC2） | `0x01` | スピーカー、マイク | OS標準 | [Audio](classes/audio.ja.md) |
| USB Video（UVC） | `0x0E` | カメラ | OS標準。**driver不要** | [Video](classes/video.ja.md) |
| CDC-NCM | `0x02`/NCM | ネットワークアダプタに見せる。USB越しのWeb UI | 主なOSは標準で対応 | [NCM](classes/ncm.ja.md) |
| CCID | `0x0B` | スマートカードリーダー | OS標準（PC/SC） | [CCID](classes/ccid.ja.md) |
| DFU | `0xFE`/DFU | `dfu-util` からのfirmware更新 | Windowsでは下記のWinUSBが要る | [DFU](classes/dfu.ja.md) |
| Vendor Specific（bulk） | `0xFF` | 独自protocol、大きなbulk転送 | **WindowsではWinUSBの割当てが要る** | [Vendor](classes/vendor.ja.md) |

判断の基準：

1. **Hostにdriverを入れさせたくないなら、標準classにします。** HID、CDC、MIDI、MSCは主なOSでそのまま使えます。
2. **独自データを流したいが、driverも入れたくないなら、HIDのVendor reportにします。** 市販の機器にも多い形です。速度は出ませんが、権限もdriverもなしで双方向に通信できます。
3. **帯域が要るならVendor（bulk）にします。** WindowsではWinUSBを割り当てるために、Microsoft OS 2.0 descriptorが要ります。このライブラリは、VendorかDFUのFunctionがあれば、WebUSBの有無に関係なく自動で返します。
4. **既存のアプリにつなぎたいなら、そのアプリが期待するclassにします。** DAWならMIDI、端末ソフトならCDCです。

複数のFunctionを組み合わせた複合機器も作れます。ただしendpointとFIFOの予算、HostのOSの都合が関わるので、まず単機能で動かしてから広げてください（[Step 7](#step-7-複合機器へ広げる)）。

### 1.8 VID・PIDとその周りのidentity

device descriptorには16 bitの数値が2つあります。**vendor ID**（誰が作ったか）と**product ID**（その会社のどの製品か）です。これに**serial number文字列**を加えた3つが、Hostが「前に見たあのDevice」と「別のDevice」を区別する鍵です。ほかのdescriptorの項目と違い、**Hostはこれらを記憶します**。

**何も設定しないとどうなるか。** 既定は `vid = 0x303A`、`pid = 0x4000` です。`0x303A` はEspressifのvendor ID、`0x4000` はESP-IDFのTinyUSB componentが既定で使うPIDの範囲（`0x4000`〜`0x4007`）の先頭です。Espressif自身も、自社VIDの利用を「製品の開発段階で役に立つ」と説明しています。既定値は、作業台での試験とこのリポジトリの例のためのものです。**あなたの製品の番号ではありません。** 人に渡す機器に載せないでください。同じVID:PIDの別製品は、Windowsからは同じDeviceに見えます。片方のdriverと記憶された設定が、もう片方に当たります。

**本物の番号はどこで得るか。** vendor IDはUSB Implementers Forum（USB-IF）が割り当てます。VID単体でUS$6,000、年会費US$5,000の会員なら会費に含まれ、USBロゴの使用許諾は別料金です。現行の規約ではVIDの譲渡・共有はできず、その下のPIDはVIDの持ち主が割り当てます。小さなプロジェクトには現実的な額ではないので、番号を使わせてくれる場所が3種類あります。

- **chip vendorの登録制度。** Espressifは、自社のUSB対応chipを載せた機器向けに [espressif/usb-pids](https://github.com/espressif/usb-pids) を運営しています。`allocated-pids.txt` に機器の説明を書いたpull requestを出すと、`0x303A` の下のPIDがもらえます。無償です。ただしEspressifは「いかなる理由でもpull requestを拒否する権利を留保する」とし、承認を「marketing資料に使うべきではない」とし、全体を「別々の製品が同じ番号を選ばないための協力的な方法」と説明しています。
- **open source向けの登録制度。** [pid.codes](https://pid.codes/howto/) は、hardwareかfirmwareを認められたopen source licenseで公開しているプロジェクトに、VID `0x1209` の下のPIDを割り当てます。[Openmokoの登録制度](https://github.com/openmoko/openmoko-usb-oui)は `0x1D50` で同じことをしています。pid.codesは、VIDが「USB-IFからVIDを発行され、その後廃業した会社から譲られたもの」で、「USB-IFが譲渡を禁じる規約に変える前に」得たものだと公表しています。どちらも有志がpull requestを手作業で処理しており、申請からmergeまで数か月かかった例があります。
- **試験用の番号。** pid.codesは `0x1209:0x0001`〜`0x0010` を試験用に確保しています。作業台では誰でも使えますが、製品に載せて出荷はできません。

どれもUSB-IFの割当てではありません。VIDを持つ誰かが、先に来た人と重ならないように、その下の番号を1つ使わせてくれているだけです。それを理解したうえでなら、USB-IFの認証を求めない製品には十分です。

**製品に要るPIDは1つです。** PIDは「Hostにどう見せたい製品か」を表す番号で、firmwareの構成を表す番号ではありません。WindowsのDevice instanceはVID・PID・serialで識別され、driverの割当ては列挙のたびにdescriptorに従います（[6.2](#62-windows)）。同じPIDのまま、Functionを足したり、動かしたり、戻したりできます。PIDをもう1つ使う理由は次の2つだけです。

- 人やHostのアプリから見て、**別の製品**に見えるべきとき。別のmodelや、アプリと絶対に混同してはならないbootloaderなどです（ESP32自身のROM bootloaderが別PIDなのはこのためです）。
- Hostに「初めて会うDevice」として扱わせ、記憶された設定を全部白紙にしたいとき。個体単位なら、serialを変えれば同じことができます。

**serial numberが3つ目の鍵です。** `serialNumber` を出せば、1台の物理的なDeviceは、どのportに挿しても1つのinstanceになります。文字列は個体ごとに一意にし、firmwareを更新しても変えないでください。出さないと、Windowsはinstanceをportの経路で識別します。同じ基板でも挿し口が変われば別のDeviceになり、設定も別になります。複数台を同時に挿す可能性があるなら、Hostが個体を見分ける手段もserialです。

`EspUsbDeviceConfig` でHostから見える残りの項目（`deviceVersion`、`product`、`manufacturer`、device interface GUID、電源の申告など）がWindowsでどう扱われるかは[6.2](#62-windows)にまとめています。

---

## 2. EspUsbDeviceの基本の流れ

### 2.1 DeviceとFunction

v3のAPIは2種類のobjectで組み立てます。

| object | 役割 | 例 |
| --- | --- | --- |
| **Device**（`EspUsbDevice`） | USB全体の所有者。controller、EP0、descriptor、USB workerを持つ | 1つのスケッチに1つ |
| **Function** | Hostに見せる機能1つ。constructorでDeviceに登録される | `EspUsbDeviceHidKeyboard`、`EspUsbDeviceCdcSerial`、`EspUsbDeviceMsc` など |

使える主なFunctionは次のとおりです。

| 分野 | class |
| --- | --- |
| HID | `EspUsbDeviceHidKeyboard`、`EspUsbDeviceHidMouse`、`EspUsbDeviceHidGamepad`、`EspUsbDeviceHidConsumerControl`、`EspUsbDeviceHidSystemControl`、`EspUsbDeviceHidCustom`、`EspUsbDeviceHidVendor` |
| CDC | `EspUsbDeviceCdcSerial` |
| Vendor | `EspUsbDeviceVendor` |
| MIDI | `EspUsbDeviceMidi` |
| MSC | `EspUsbDeviceMsc`。helperとして `EspUsbDeviceMscRamDisk`、`EspUsbDeviceMscFatRamDisk`、`EspUsbDeviceMscSdCard`、`EspUsbDeviceMscFirmwareDisk` |
| Audio | `EspUsbAudioFunction`（UAC1／UAC2） |
| Video | `EspUsbDeviceVideo` |
| NCM | `EspUsbDeviceNet` |
| CCID | `EspUsbDeviceCcid` |
| DFU | `EspUsbDeviceDfu`。helperとして `EspUsbDeviceFirmwareUpdate` |

- DeviceはFunctionを所有しません。DeviceはFunctionより長く生かしてください。普通はどちらもglobal変数にします。
- 同じ種類のFunctionを複数置けます（CDCを2つ、など）。
- Functionの数に固定の上限はありません。上限はendpointとFIFOの予算で決まります（[4.4](#44-endpointの予算)）。文字列indexの都合で、251個を超えると `begin()` が失敗します。

### 2.2 configure、begin、end

流れは「Functionを作る → 必要なら `configure()` → `device.begin()`」です。

```cpp
#include <EspUsbDevice.h>

EspUsbDevice device;                                // USB全体の所有者
EspUsbDeviceHidKeyboard keyboard(device);           // constructorでdeviceに登録される
EspUsbDeviceCdcSerial console(device, "Console");   // 名前はHostに見える

void setup() {
  Serial.begin(115200);  // ログ用。USB-UARTかUSB Serial/JTAGの口に出る

  EspUsbBulkStreamConfig buffers;  // 容量はFunctionごとに実行時に決める
  buffers.rxCapacity = 1024;
  buffers.txCapacity = 1024;
  if (console.configure(buffers) != ESP_OK) {
    Serial.println("configure failed");
  }

  EspUsbDeviceConfig config;
  config.product = "My Device";
  if (!device.begin(config)) {
    Serial.printf("begin failed: %s\n", device.lastErrorName());
  }
}

void loop() {
  if (device.ready() && console.available()) {
    console.write(console.read());
  }
  delay(1);
}
```

| 段階 | すること | 失敗の返し方 |
| --- | --- | --- |
| Functionのconstructor | Deviceへ登録する。heapは確保しない | なし |
| `configure()` | 容量や方式を設定する。値の形だけを検査する | `esp_err_t` を返す |
| `device.begin(config)` | descriptorを作り、controllerの予算を検査し、各Functionの資源を確保し、USBを開始する | `false` を返し、理由は `lastError()` |
| 動作中 | 転送はUSB worker（FreeRTOS task）が処理する。`task()` やpollingの呼出しは要らない | 各Functionの戻り値 |
| `device.end()` | USB workerとDMAを止め、借用bufferを返し、資源を全部解放する | `esp_err_t` を返す。workerが期限内に止まらなければ `ESP_ERR_TIMEOUT` |

- **USBは自動では始まりません。** `begin()` を呼ぶまで、Hostには何も見えません。
- **設定を変えるときは止めてから変えます。** `end()` → `configure()` → `begin()` の順です。Hostには「抜いて挿し直した」と見えます。
- **動作中のFunctionの追加は拒否されます。** 拒否されたFunctionが残ったままだと、次の `begin()` も `ESP_ERR_INVALID_STATE` で失敗します。止めてから作り直すか、`device.addClass(&function)` で登録し直してください。
- **`begin()` の失敗は、取得した資源を全部戻してから返ります。** 設定を直して、もう一度 `begin()` を呼べます。
- **`end()` はworkerの停止を `config.stopTimeoutMs`（既定5000 ms）まで待ちます。** callbackが戻らず期限を過ぎると `ESP_ERR_TIMEOUT` を返します。停止の要求は残るので、callbackが戻った後にもう一度 `end()` を呼べば停止が終わります。`0` は期限なしで待ちます。DeviceとFunctionのdestructorは、停止が終わるまで待ちます。
- **Functionの `begin()` は呼べません。** Functionの開始と停止は `device.begin()`／`device.end()` だけが行います。`console.begin()` のように書くとcompile errorになります。

### 2.3 状態：started、ready、suspended

| 関数 | trueになる条件 |
| --- | --- |
| `started()` | `begin()` が成功してから `end()` まで。資源を持っていることを表す。Hostの接続待ちやreset中もtrue |
| `ready()` | HostがSET_CONFIGURATIONを済ませた（列挙の手順7） |
| `suspended()` | busが休止している（3 ms以上通信がない。USB 2.0 §7.1.7.6）。休止中もconfigurationは保たれるので、`ready()` はtrueのまま |

`device.speed()` は、Hostが最後のbus resetで決めた速度を返します（USB 2.0 §7.1.7.5）。

| 値 | 意味 |
| --- | --- |
| `EspUsbBusSpeed::None` | USB開始前、descriptorだけを作るmode、`end()` 後、まだbus resetがない |
| `EspUsbBusSpeed::Full` | 12 Mbps。HS controllerでも、FSのhubやHostにつなぐとこれになる |
| `EspUsbBusSpeed::High` | 480 Mbps |

- `suspended()` は、USB開始前、descriptorだけを作るmode（[2.6](#26-usbを起動せずにdescriptorを確かめる)）、`end()` 後はfalseです。
- 休止はresetでも停止でもありません。借用中のbufferは、完了・取消し・reset・`end()` のどれかで返るまで保持されます。
- `ready() && !suspended()` でも、次の転送が成功するとは限りません。送れたか、届いたかは、各Functionの戻り値と完了通知で判断してください。
- Hostを休止から起こしたい場合は、`config.remoteWakeupSupported = true` で能力を申告し、Hostが許可した後（`remoteWakeupEnabled()`）に `remoteWakeup()` を呼びます。使うかどうかを決めるのはHostです（USB 2.0 §9.4.1、§9.4.9）。

### 2.4 容量は実行時に決める

v3では、bufferの容量を**Functionごとに、実行時に**決めます。build flagは使いません。

| Function | 設定の型 | 主な項目と既定 |
| --- | --- | --- |
| CDC、Vendor | `EspUsbBulkStreamConfig` | `rxCapacity` 512、`txCapacity` 512、`transferBytes` 512 |
| MIDI | `EspUsbMidiConfig` | `rxPackets` 128、`txPackets` 128 |
| MSC | `EspUsbMscConfig` | `transferBytes` 512（bulk MPSの倍数が必要） |
| Audio | `EspUsbAudioConfig` | `playbackBufferFrames` 256、`captureBufferFrames` 256、`eventCapacity` 8 |
| Video | `EspUsbVideoConfig` | `transport` Isochronous、`payloadBytes` 512 |
| NCM | `EspUsbNetConfig` | `ntbInBytes`／`ntbOutBytes` 3200 |
| CCID | `EspUsbCcidConfig` | `messageBytes` 0（自動。`max(271, Bulk-OUT MPS)`） |
| DFU | `EspUsbDfuConfig` | `transferBytes` 1024 |

メモリの使い方：

- **確保は `begin()`、解放は `end()` です。** 使うFunctionの、選んだ容量の分だけ確保します。転送の途中ではheapを確保しません。
- **使わないFunctionはlinkされません。** 作らなかったFunctionのコードとbufferは、firmwareに入りません。
- **USBのDMAが触る領域は、DMA可能な内部RAMです。** ringやMSCの保存先のように、CPUだけが読み書きする領域にはPSRAMを渡せます（`rxBuffer`、`txBuffer` など）。
- **借用転送（`writeDirect`／`readDirect` など）のbufferは、DMA可能な内部RAMで、64 byte境界に置きます。** PSRAMを渡すと `NotDmaCapable`、境界がずれていると `NotAligned` で拒否されます。拒否されたbufferは借用されません。

各Functionの容量、copy方式と借用方式の違いは、`docs/classes/` の各ガイドを見てください。

### 2.5 callbackとUSB worker

USBの処理は、`loop()` とは別のUSB worker（FreeRTOS task）で動きます。Functionに登録したcallbackも、このworkerの上で呼ばれます。

- **callbackは短くしてください。** 長いログ、待機、file I/Oを入れると、次のUSB処理が遅れます。重い処理は、queueなどで `loop()` 側へ渡します。
- **callbackの中で待たないでください。** HIDの送信は `timeoutMs = 0` で呼びます。workerの中から待つ送信は拒否されます。
- **callbackの中で `begin()`／`end()` や再設定をしないでください。** callbackから `end()` を呼ぶと `ESP_ERR_INVALID_STATE` になります。
- callbackに渡されたdataは、戻ると再利用されることがあります。後で使う分は自分のbufferへコピーします。

workerの設定は `EspUsbDeviceConfig` で変えられます。既定のままなら何も書かなくてかまいません。

| 項目 | 既定 | 意味 |
| --- | --- | --- |
| `taskStackBytes` | 4096 | workerのstack（byte）。callbackもこのstackを使う |
| `taskPriority` | -1 | -1は `configMAX_PRIORITIES - 1`（最高） |
| `taskCoreId` | -1 | -1はcoreを固定しない |
| `eventQueueDepth` | 16 | 割込みから受け取ったeventを一時的に保持する数。0は不可 |
| `stopTimeoutMs` | 5000 | `end()` がworkerの停止を待つ時間（ms）。0は期限なし |

### 2.6 USBを起動せずにdescriptorを確かめる

`startMode = DescriptorsOnly` にすると、`begin()` はdescriptorを作り、Functionの資源を確保しますが、controller、割込み、workerは起動しません。Hostなしで構成を検査できます。

```cpp
EspUsbDeviceConfig config;
config.startMode = EspUsbDeviceStartMode::DescriptorsOnly;
if (device.begin(config)) {
  const uint8_t *descriptor = device.configurationDescriptor(0);
  // started() == true、ready() == false
  device.end();
}
```

endpointとFIFOの予算の検査も、このmodeで行われます。予算を超えれば、ここで `ESP_ERR_INVALID_SIZE` になります。

### 2.7 エラーの読み方

`begin()` は `bool` を返し、失敗の理由は `device.lastError()`（`esp_err_t`）に残ります。`device.lastErrorName()` はその名前の文字列です。

```cpp
if (!device.begin(config)) {
  Serial.printf("begin failed: %s (0x%x)\n",
                device.lastErrorName(), unsigned(device.lastError()));
}
```

| エラー | 主な原因 |
| --- | --- |
| `ESP_ERR_INVALID_SIZE` | endpointの予算を超えた。IN endpointが送信FIFOに入らない（[4.5](#45-fifoの予算)）。HIDのpacketがFSで64、HSで1024 byteを超えた。文字列が126 UTF-16 code unitを超えた。MSCの `transferBytes` がbulk MPSの倍数でない。CCIDの明示 `messageBytes` がBulk-OUT MPS未満。VendorのFS bulk長が8/16/32/64以外。`bulkInBuffering = Double` がFIFOに入らない。`taskStackBytes` が小さすぎる |
| `ESP_ERR_NO_MEM` | heapが足りない |
| `ESP_ERR_INVALID_ARG` | 設定値が不正（`startMode`、`taskPriority`、`taskCoreId`、GUIDの形など）。Audioの設定の組合せが合わない |
| `ESP_ERR_INVALID_STATE` | すでに開始している。別の `EspUsbDevice` が動いている。登録を拒否されたFunctionが残っている。USB workerのcallbackから `end()` を呼んだ |
| `ESP_ERR_NOT_SUPPORTED` | このSoCにUSB OTGがない。指定した速度のcontrollerがない（S3で `HighSpeed` を選んだなど） |
| `ESP_FAIL` | 割込みの登録、cacheの同期など、hardware側の準備に失敗した |

- **`lastError()` は `begin()` の直後に読んでください。** `addClass()` やHIDの送信など、ほかの呼出しでも書き換わります。
- **`lastErrorName()` は `esp_err_to_name()` と同じ名前を返します。** 上の表にない値（HIDの送信待ちが時間切れになったときの `ESP_ERR_TIMEOUT` など）も名前で読めます。
- `configure()` は自分で `esp_err_t` を返します。`begin()` の中でFunctionが失敗した場合は、そのFunctionの理由が `lastError()` に入ります。
- 失敗はPHYやHostに触る前に検出するので、`begin()` が失敗している間はHost側には何も起きていません。まず手元の設定を疑ってください。

---

## 3. 電源とコネクタ

### 3.1 DeviceはVBUSをもらう側

Host側の開発では「VBUSを出せるか」が最初の関門でした。Device側では逆で、**HostからVBUSをもらいます**。電源そのものでつまずくことは少ないのですが、次のことは起きます。

- **OTGコネクタのVBUSがSoCの電源につながっていないボードがあります。** そのコネクタだけを挿しても起動しません。ログ用のコネクタから別に給電してください。
- **自己給電なら申告します。** 別電源で動くなら `config.selfPowered = true` にします。descriptor上の申告で、実際の配線と一致させてください。Hostの電力管理がこれを見ます。
- **VBUSの検出は既定で使いません。**

#### vbusSensing

| 値 | 動作 |
| --- | --- |
| `false`（既定） | VBUSが常にあるものとして動く。検出回路のないボード向け |
| `true` | ボードのVBUS検出信号を使う |

- `true` は、ボードに検出回路があり、その信号がcontrollerへ配線されている場合だけ選びます。このoptionはGPIOや回路を用意しません。
- 多くのESP32ボードはVBUS検出を配線していません。`false` のままでは、ケーブルを抜いたことを、次にHostがbusをresetするまで知る手段がない場合があります。
- 旧P4（chip revision v1.x）のHS portでは、session-valid（BSesVld）のbitがVBUSの有無を示しませんでした。実際にケーブルを抜いたときのVBUS検出は、まだ試験していません。

### 3.2 bMaxPower

`config.maxPowerMilliamps`（既定100 mA）は、busから取ると申告する電流です。descriptorには2 mA単位で入り、500 mAが上限です（USB 2.0 §9.6.3）。Wi-Fiを使うなど消費が大きい構成なら、500 mAまで上げるのが素直です。Hostによっては、申告を超えて消費するとportを落とします。

### 3.3 開発中のコネクタの使い方

| 用途 | つなぐ先 |
| --- | --- |
| 書込み・ログ | USB-UARTブリッジ、またはUSB Serial/JTAG |
| 検証するHost | ESP32のUSB OTGコネクタ |

コネクタが1つしかないボード（AtomS3など）を最終製品にするのはかまいません。ただし**開発中は2口のボードを使ってください**。列挙した瞬間にログが切れると、`ready()` の変化すら読めません。

もう一つの方法は、Host役に別のESP32（[EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost)）を使うことです。両側のログをPCから同時に読めます。このリポジトリの試験（[validation](../validation/README.ja.md)）はこの構成です。

コネクタが1つのESP32-S3でも、同じケーブルのまま書き込めます。resetすると共有PHYがUSB Serial/JTAGへ戻り、ROMのloaderが出てくるからです。動作中のスケッチからBOOTボタンなしでそこへ入る方法（`device.rebootToBootloader()`）は[USB経由のfirmware更新](ota-over-usb.ja.md)にあります。

---

## 4. ESP32シリーズ固有の注意点

### 4.1 対応chipとcontroller

| chip | USB Deviceの能力 | `EspUsbController::Auto` の選択 | 状態 |
| --- | --- | --- | --- |
| ESP32-S3 | FSのcontroller 1つ | FullSpeed | 主な対象。実機で確認 |
| ESP32-S2 | FSのcontroller 1つ | FullSpeed | compileのみ確認。実機は未確認 |
| ESP32-P4（旧revision v1.x） | HSとFSのcontroller 1つずつ | **HighSpeed** | 実機で確認 |
| ESP32-P4X（revision v3.x） | 同上 | **HighSpeed** | 実機で確認 |
| ESP32-S31 | — | — | 未対応 |
| ESP32（無印）、C3、C6など | USB OTGがない | — | 動かない |

基準のArduino-ESP32は**3.3.12**です。

### 4.2 ESP32-P4のHSとFSの選び方

`config.controller` で選びます。

| 値 | 選ぶcontroller |
| --- | --- |
| `Auto`（既定） | HS controllerがあればHS（P4）、なければFS（S2/S3） |
| `HighSpeed` | HS controller。なければ `begin()` が `ESP_ERR_NOT_SUPPORTED` |
| `FullSpeed` | FS controller |

| | FS controller | HS controller |
| --- | --- | --- |
| PHY | SoC内蔵のFS PHY | UTMI HS PHY |
| 既定のpin | GPIO26＝D−、GPIO27＝D+ | ボード上のUTMI配線 |
| 速度 | 12 Mbps | 480 Mbps（FSのHostにつなぐとFS） |
| endpoint番号 | 1〜6 | 1〜15 |
| 非controlのIN endpoint | 4本 | 7本 |
| FIFO | 256 word（1 KB） | 1024 word（4 KB） |

選び方：

- **帯域が要る、Functionが多い、大きなpacketを使う** → HS。
- **ボードのFS用コネクタにつなぐ、FSの構成をそのまま移したい** → FS。
- どのコネクタがどちらのcontrollerに配線されているかは**ボードごとに違います**。回路図で確認してください。USB Serial/JTAGのコネクタでもGPIO26/27のFSペアでもない3つ目のコネクタがHS用、という構成が普通です。
- `config.controller` と、ケーブルを挿したコネクタが合っていないと、`begin()` は成功してもHostには何も見えません。

FS PHYはUSB Serial/JTAGとpinを共有しています。`usb_wrap_ll_phy_select(&USB_WRAP, 0)` でFS controllerをGPIO24/25側へ移せます（[`P4FullSpeedDevice`](../examples/P4FullSpeedDevice/README.ja.md)にコメントで記載）。移すとUSB Serial/JTAGがGPIO26/27へ移るので、GPIO24/25で使っていたSerial/JTAGの接続は切れます。

#### 旧P4とP4X

ライブラリのsourceに、旧P4とP4Xを区別する処理はありません。違いはbuildの設定です。

| | 旧P4（v1.x） | P4X（v3.x） |
| --- | --- | --- |
| Arduinoの `ChipVariant` | `prev3` | `postv3` |
| examplesのprofile | `esp32p4` | `esp32p4x` |
| SDKが対応するrevision | v1.x | **v3.1以上** |

- boardメニューの表示は「v3.00 or newer」ですが、配布SDKの最小revisionは3.1です。v3.0の実機は対応済みとして扱いません。
- endpoint数とFIFOの容量は両者で同じです。

### 4.3 HSで変わる値

HSではbulkのMPSが64から512 byteに、interruptの上限が64から1024 byteになります（USB 2.0 §5.7.3、§5.8.3）。そのため、**FSで動いていた設定が、HSでは `begin()` で失敗することがあります。**

| 対象 | HSでの条件 |
| --- | --- |
| MSCの `transferBytes`（既定512） | 512の倍数が必要。64や256は `ESP_ERR_INVALID_SIZE` |
| CCIDの `messageBytes` | 既定0（自動）は512になる。512未満の明示値は `ESP_ERR_INVALID_SIZE` |
| 借用受信（`readDirect`）の容量 | 512の倍数が必要（FSでは64の倍数） |
| CDC、Vendor、MIDIの `transferBytes` | 512未満でも受け付ける |
| Vendorのbulk packet長 | 常に512（constructorのFS用の値は使わない） |
| HIDのinterrupt endpoint | 1024 byteまで使える（`EspUsbDeviceHidCustom`、`EspUsbDeviceHidVendor`）。実際の値は `inputPacketSize()` で読める |
| UAC2のfeedback | 16.16形式の4 byte（FSは10.14形式の3 byte。USB 2.0 §5.12.4.2） |

HS controllerを選ぶと、linkがFSになった場合でもHSのMPSを扱えるように、上の条件で検査します。

### 4.4 endpointの予算

**Device側で最初にぶつかる制限です。** controllerが持てるendpointの番号と本数には上限があります。超える構成は、`begin()` がPHYを起動する前に `ESP_ERR_INVALID_SIZE` で拒否します。

| controller | endpoint番号の上限 | 非controlのIN | 非controlのOUT |
| --- | --- | --- | --- |
| ESP32-S2/S3 | 6 | 4本 | 6本 |
| ESP32-P4 FS | 6 | 4本 | 6本 |
| ESP32-P4 HS | 15 | 7本 | 15本 |

1つの番号をINとOUTで共有できます。Functionごとの消費は次のとおりです。

| Function | IN | OUT |
| --- | --- | --- |
| HID（全HID Functionをまとめて1 interface） | 1 | Output reportがあれば1 |
| CDC ACM | 2（通知、データ） | 1 |
| CDC-NCM | 2（通知、データ） | 1 |
| Vendor | 1 | 1 |
| MIDI | 1 | 1 |
| MSC | 1 | 1 |
| CCID | 1（取外しできるslotなら通知用にもう1） | 1 |
| Audio | captureで1、UAC2のplaybackでfeedback用に1 | playbackで1 |
| Video | 1 | 0 |
| DFU | 0（EP0だけを使う） | 0 |

効くのは、S2/S3とP4 FSの**IN 4本**です。

- HID + CDC + MSCは、INが1 + 2 + 1 = 4本で、ちょうど上限です。Vendorを足すと入りません。
- CDCを2つなら4本で入ります。3つは6本で入りません。
- DFUはendpointを使わないので、予算を使い切った構成にも足せます。

収まるかは、[`EspUsbDeviceDescriptorDump`](../examples/Info/EspUsbDeviceDescriptorDump/README.ja.md) の `DUMP_ENABLE_*` を目的の構成にしてbuildし、最後の `endpoint budget` を読めばわかります。Hostにつなぐ必要もありません。

### 4.5 FIFOの予算

endpointの本数とは別に、controller内の**FIFO**も分け合います。FIFOは受信用と、全IN endpointの送信用で共有する固定の領域です。RAMを増やしても広がりません。

| controller | FIFO | IN endpointに残る量の目安 |
| --- | --- | --- |
| S2/S3、P4 FS | 256 word（1 KB） | 約650 byte（OUTの最大packetが64 byteのとき） |
| P4 HS | 1024 word（4 KB） | 約2.6 KB（OUTの最大packetが512 byteのとき） |

目安は、受信FIFO（固定分＋OUTの最大packet＋endpointごとの分）とEP0の分を引いた残りです。OUTに大きなpacket（Audioのplaybackなど）があると、受信FIFOが増えて残りは減ります。

- **`begin()` は、構成にある全IN endpointの送信FIFOを計算します。** 入らなければ `ESP_ERR_INVALID_SIZE` で失敗します。入らないまま始めると、SET_CONFIGURATIONでendpointが開けないか、isochronous INでは列挙もdriverの割当ても成功するのに何も届かないDeviceになるからです。検査はPHYに触る前に行います。
- 例：P4 HSでは、Vendor（bulk IN 512 byte）は5個まで入ります。6個はendpointの本数には収まりますが、FIFOに入らず `ESP_ERR_INVALID_SIZE` になります。FSでは、カメラ（既定のpayload 512 byte）とAudioのcaptureを同時に置くと、この上限に当たりやすくなります。
- **bulk INの二重buffer**（`config.bulkInBuffering`）は、FIFOに余裕があるとき、bulk INを2 packet分にして連続送信を速くします。

| 値 | 動作 |
| --- | --- |
| `Auto`（既定） | 全bulk INが2 packet分に入るならそうする。入らなければ何も変えない |
| `Single` | 1 packet分 |
| `Double` | 全bulk INを2 packet分にする。入らなければ `begin()` が `ESP_ERR_INVALID_SIZE` |

実際に2 packet分になったendpointは `device.bulkInDoubleBuffered()`（endpoint番号のbitmap）で読めます。連続送信が期待より遅いときは、ここが0になっていないかを見てください。

### 4.6 その他の上限

| 項目 | 上限 | 超えたとき |
| --- | --- | --- |
| Functionの数 | 251 | `begin()` が `ESP_ERR_INVALID_SIZE` |
| 文字列（manufacturer、product、serial、Function名） | 1つあたり126 UTF-16 code unit | `begin()` が `ESP_ERR_INVALID_SIZE`。切り詰めない |
| configuration descriptor | 65535 byte（`wTotalLength` が16 bit） | `begin()` が `ESP_ERR_INVALID_SIZE` |
| WebUSBのURL | `http(s)://` を除いて252 byte | `begin()` が `ESP_ERR_INVALID_SIZE` |

文字列はUTF-8で渡せます。`begin()` がUTF-16へ変換してコピーするので、日本語も使えます。不正なUTF-8は拒否します。

### 4.7 Arduino-ESP32の標準USBとは同時に使えない

このライブラリは、OTG controllerを自分で初期化して使います。Arduino-ESP32の標準USB stackと同じcontrollerを取り合うので、同時には使えません。

- **`USB.begin()` を呼ばないでください。** `USBHIDKeyboard`、`USBHIDMouse`、`USBCDC` なども併用できません。
- **ESP32-S3では、boardメニューの `USB Mode` を「USB-OTG (TinyUSB)」にしてください。** S3の既定は「Hardware CDC and JTAG」で、このときD+/D−はUSB Serial/JTAGへ回ります。`arduino-cli` では `esp32:esp32:esp32s3:USBMode=default` が「USB-OTG (TinyUSB)」です。
- **`USB CDC On Boot`、`USB MSC On Boot`、`USB DFU On Boot` は無効のままにしてください。**
- **ESP32-P4では、HS controllerはUSB Serial/JTAGと別です。** examplesは `USBMode=default` でbuildします。試験では、P4Xを `USBMode=hwcdc,CDCOnBoot=cdc` でbuildしてログをUSB Serial/JTAGへ出し、HS controllerをEspUsbDeviceに使う構成も確認しています。
- **TinyUSBのbuild flag（`CFG_TUD_*` など）は使いません。** v3はTinyUSBのdevice stackを使わず、自前のcoreで動きます。`build_opt.h` などに旧flagが残っていると、移行を促すcompile errorになります（0を指定しても同じです）。

S3でUSB OTGを使う構成では、`Serial` はUART側に出ます。ログの口を別に用意する必要があるのはこのためです（[3.3](#33-開発中のコネクタの使い方)）。

### 4.8 ログ

`begin()` の失敗は、まず `lastErrorName()` を表示してください（[2.7](#27-エラーの読み方)）。列挙の失敗は、Device側からは見えないことが多いので、**Host OS側のログも必ず見てください**（[6章](#6-ホストosから自分を観測する)）。Device開発では、Hostが何を受け取り、何を嫌がったかが一番の情報源です。

---

## 5. 立ち上げ手順

新しいUSB機器を作るまでの手順です。**上から順に、飛ばさずに**進めてください。失敗の多くは、前の段階の問題（コネクタ、build設定、予算）を、後の段階で悩んでいることから起きます。

### Step 0. 机の上の確認（コードを書く前）

- 回路図で、**どのコネクタがESP32のUSB OTGにつながっているか**を確かめる。P4ではHSとFSのどちらかも確かめる
- ログ用のコネクタと、Hostにつなぐコネクタを分けられるか確かめる
- ケーブルがデータ線入りか（充電専用でないか）確かめる
- boardの設定を確かめる（S3は `USB Mode` が「USB-OTG (TinyUSB)」。P4は `ChipVariant` がchipのrevisionと合っているか）

### Step 1. 起動と列挙を確かめる

[`EspUsbDeviceBringUpCheck`](../examples/Info/EspUsbDeviceBringUpCheck/README.ja.md) を書き込みます。HIDキーボードとして列挙しますが、キーは送らないので、PCに挿したままでも安全です。

- `BEGIN ok` が出るか → 出なければ、表示されたエラー名を[2.7](#27-エラーの読み方)で調べる。Hostはまだ関係していない
- Hostにつないで `MOUNTED` が出るか → 出なければ、電気的な問題かdescriptorの問題。10秒後にchecklistが出る
- Host側でCapsLockを押して `HOST_OUTPUT_REPORT` が出るか → Host→Deviceの向きも通っている
- Hostを休止させると `SUSPENDED` が出るか

ここまで通れば、ボードとbuild設定は正常です。以後の問題は、自分の構成に固有のものだと切り分けられます。

### Step 2. 何に見せるかを決める

[1.7の表](#17-何に見せるかを選ぶ)から、目的に合うclassを選びます。この選択がプロジェクト全体を決めるので、先に決めてください。

### Step 3. 例で最小構成を動かす

選んだclassの例を、そのまま書き込みます。

| class | 例 |
| --- | --- |
| HIDキーボード／マウス | [`Keyboard`](../examples/Keyboard/README.ja.md)、[`Mouse`](../examples/Mouse/README.ja.md) |
| HIDゲームパッド／メディアキー | [`Gamepad`](../examples/Gamepad/README.ja.md)、[`MediaKeys`](../examples/MediaKeys/README.ja.md) |
| HIDの独自report | [`CustomHID`](../examples/CustomHID/README.ja.md)、[`VendorHID`](../examples/VendorHID/README.ja.md) |
| CDC ACM | [`Serial`](../examples/Serial/README.ja.md)、[`SerialMulti`](../examples/SerialMulti/README.ja.md) |
| USB MIDI | [`MIDI`](../examples/MIDI/README.ja.md)、[`MIDIController`](../examples/MIDIController/README.ja.md) |
| MSC | [`MSC`](../examples/MSC/README.ja.md)、[`MSCFatRamDisk`](../examples/MSCFatRamDisk/README.ja.md)、[`MSCSdCard`](../examples/MSCSdCard/README.ja.md) |
| USB Audio | [`AudioSpeaker`](../examples/AudioSpeaker/README.ja.md)、[`AudioMicrophone`](../examples/AudioMicrophone/README.ja.md)、[`AudioHeadset`](../examples/AudioHeadset/README.ja.md) |
| USB Video | [`VideoCamera`](../examples/VideoCamera/README.ja.md) |
| CDC-NCM | [`UsbNetwork`](../examples/UsbNetwork/README.ja.md) |
| CCID | [`SmartCardReader`](../examples/SmartCardReader/README.ja.md) |
| Vendor bulk | [`USBVendor`](../examples/USBVendor/README.ja.md) |
| DFU、firmware更新 | [`FirmwareDFU`](../examples/FirmwareDFU/README.ja.md)、[`FirmwareMSC`](../examples/FirmwareMSC/README.ja.md) |
| P4のHS／FS | [`P4HighSpeedDevice`](../examples/P4HighSpeedDevice/README.ja.md)、[`P4FullSpeedDevice`](../examples/P4FullSpeedDevice/README.ja.md) |

**例が動いてから、自分のコードを書いてください。** 動かないときに、ライブラリの問題か自分の構成の問題かを分けられます。例の読み方は[サンプルの進め方](example-workflow.ja.md)にあります。

### Step 4. 自分が何を宣言しているか確かめる

[`EspUsbDeviceDescriptorDump`](../examples/Info/EspUsbDeviceDescriptorDump/README.ja.md) の `DUMP_ENABLE_*` を自分の構成に合わせてbuildし、表示を読みます。Hostにつなぐ必要はありません。

確かめる点：

- interfaceはいくつあり、それぞれのclass／subclass／protocolは何か
- endpointの種類、方向、MPS、interval
- HID report descriptorは何byteか
- **endpointの予算に収まっているか**。bulk INの二重bufferがどうなったか

自分のスケッチの中で確かめるなら、[2.6](#26-usbを起動せずにdescriptorを確かめる)の `DescriptorsOnly` を使えます。

### Step 5. Hostが受け取った内容と突き合わせる

Step 4は「送ったつもりの内容」です。Hostが実際に受け取った内容と一致するかを確かめます。

```sh
# Linux
lsusb -v -d 303a:4051
```

一致すれば、descriptorの層は正常です。一致しない、あるいはHost側に何も出ないなら、問題はもっと下（列挙、電気、controllerの選択）にあります。詳しい観測方法は[6章](#6-ホストosから自分を観測する)にまとめています。

### Step 6. 手打ちで詰める

classは動くが「Hostのソフトが期待どおりに反応しない」段階では、[`EspUsbDeviceConsole`](../examples/Info/EspUsbDeviceConsole/README.ja.md) が役立ちます。build し直さずに、1転送ずつ試せます。

```
> state
> key 0x04                            # このusageにHostは反応するか
> report 1 00 00 05 00 00 00 00 00    # 生のreportを送る
> vendor 01 02 03 04                  # vendorのbulk INへ生のbyte
```

Hostから来るもの（HID output report、protocolの切替、vendor control request、bulk OUT）は、`HOST_` を付けて全部表示されます。**Hostの純正ソフトを動かしながらこれを眺めると、Hostが期待している初期化の手順がわかります。**

### Step 7. 複合機器へ広げる

単機能で動いたら組み合わせます。制約は2つです。

- **endpointとFIFOの予算**（[4.4](#44-endpointの予算)、[4.5](#45-fifoの予算)）。DescriptorDumpで先に確かめる
- **HostのOS**。複合機器のinterfaceの並びやIADの有無で、挙動が変わるOSがある

複数のHIDは、interfaceではなくreport IDが増えます。[`CompositeHidCdcMsc`](../examples/CompositeHidCdcMsc/README.ja.md)、[`KeyboardMouse`](../examples/KeyboardMouse/README.ja.md) が参考になります。

登録の順で決まるのは、interface番号とendpoint番号だけです。HIDを先に登録しても後に登録しても、同じように動きます。ただし、WindowsのCOMポート番号などはinterface番号に付くので、並びを変えるとHost側の見え方が変わります（[6.2](#62-windows)）。組み替えたら、対象のOSで確かめ直してください。

### Step 8. 耐性を確かめる

一度列挙できることと、使い続けられることは別です。

- Hostを休止・復帰させる（`suspended()` の変化と、復帰後の転送）
- ケーブルを何度も抜き挿しする
- `end()` → `begin()` を繰り返す（Hostからは抜き挿しに見える）
- Hostを再起動して、起動中（BIOS／UEFI）に認識されるか見る。HIDならboot protocolに切り替わる

### Step 9. 結果を残す

- 動く最小のスケッチを残す
- **確かめたHost OSとそのversion、chipとrevision、使ったcontroller（HS／FS）を書く。** Device開発では、これが一番大事な記録です

---

## 6. ホストOSから自分を観測する

Host側の開発では、相手の機器を観測しました。Device側では、**自分がHostにどう見えているか**を観測します。OSごとに道具が違います。

### 6.1 Linux

```sh
# 何が起きたか（挿す前から流しておく）
sudo dmesg -w
udevadm monitor --udev

# descriptorの全部
lsusb -v -d 303a:4050

# treeと速度（12M／480M）
lsusb -t

# HID report descriptor
sudo usbhid-dump -d 303a:4050

# HIDのキーボード／マウスとして何が届いているか
sudo evtest

# 自分宛の転送を記録する
sudo modprobe usbmon
sudo wireshark   # usbmonX を選ぶ
```

`dmesg` は特に大事です。descriptorが不正なら理由が出ますし、どのdriverが割り当てられたかもわかります。P4のHS controllerがHSで列挙されたかは、`lsusb -t` の `480M` で確かめます。Device側では `device.speed()` で読めます（[2.3](#23-状態startedreadysuspended)）。

### 6.2 Windows

- **デバイスマネージャー**：「不明なUSBデバイス（デバイス記述子要求の失敗）」はdescriptor段階の失敗、「!」付きのCode 10／Code 43はdriverの割当ての失敗です。
- **[USB Device Tree Viewer](https://www.uwe-sieber.de/usbtreeview_e.html)**：Windowsで一番役立つ道具です。descriptorの生のbyteと、Windowsが出したエラーの両方を見られます。
- **イベントビューアー**：driverの割当て失敗の詳しい理由。
- **`pnputil /enum-devices /connected`**：割り当てられたdriverの確認。

> この節の観測は、Windows 11 25H2（build 26200.9457）で、**EspUsbDevice v2**のfirmwareを使って行ったものです。Windowsの判断はdescriptorとidentityだけで決まるので、v3でも同じ規則になると考えています。ただし**v3では再測定していません**。WindowsのDriverの割当てやclassの動作も、v3では未確認です。

#### Windowsが読み直すもの、保持したままのもの

「Windowsはdescriptorをcacheする」という言い方は、大まかすぎて対策に使えません。1つずつ変えて測った結果は次のとおりです。

| 変えたもの | Windowsは追随するか |
| --- | --- |
| interface、endpoint、packet長、Functionの有無と並び | **する。毎回** |
| どのdriverが割り当てられるべきか | **する。毎回** |
| `deviceInterfaceGuid`（vendor revisionが動く場合＝既定） | **する** |
| `deviceInterfaceGuid`（vendor revisionを固定した場合） | **しない**。古いGUIDのまま |
| `deviceVersion`、`product` | **する**。hardware IDとbusが報告する説明は更新。デバイスマネージャーの表示名は据え置き |
| `vid`、`pid`、`serialNumber` | **する**。Windowsには別のDeviceなので、すべて白紙から |

- **標準descriptorは列挙のたびに読まれます。** vendor interface 1つだったものが複合機器になれば子Deviceに分かれ、戻せば1つに戻ります。driverの選択はdescriptorに追随するので、こちらから何かする必要はありません。
- **例外はMicrosoft OS 2.0のregistry propertyです。** Windowsは `DeviceInterfaceGUIDs` をinstanceごとに一度読んで保持し、descriptor setの `wVendorRevision` が変わったときだけ読み直します。このライブラリは既定でrevisionをdescriptor setの内容から作るので、setが変われば自動で読み直されます。`msOs20VendorRevision` を手で固定した場合だけ、古いGUIDが残ります。
- **identityを変えれば白紙からです。** WindowsはinstanceをVID・PID・serialで識別します（`USB\VID_303A&PID_4080\GUID-TEST-1`）。どれかを変えれば新しいinstanceになります。古いinstanceはregistryに残りますが、使われません。
- **`serialNumber` を出さないと、instanceはportの経路で識別されます**（`USB\VID_303A&PID_4080\8&2EBC545B&0&4`）。同じ基板でも、挿し口が変わればWindowsには別のDeviceです。
- **複合機器の子instanceはinterface番号に付きます**（`USB\VID_303A&PID_4090&MI_01\...`）。設定を保持するのはその子です。アプリがGUIDで列挙して得るpathも、Functionとともに動きます。毎回GUIDで列挙し、pathを保存しないでください。

**COMポートはinterface番号に付きます。** CDC 1つを、同じPID・serialで測った結果：

| 構成 | CDCの子 | COM |
| --- | --- | --- |
| CDCだけ | `MI_00` | COM16 |
| CDC + vendor（CDCを先に登録） | `MI_00`、同じinstance | **COM16のまま** |
| vendor + CDC（vendorを先に登録） | `MI_01`、**新しいinstance** | **COM34** |
| CDCだけに戻す | `MI_00` | COM16に戻る |

シリアルポートの*後ろ*にFunctionを足しても、COM番号は動きません。シリアルポートを別のinterface番号へ動かすと新しい番号になります。利用者の端末ソフトにCOM16が保存されているなら、更新後も使えるかどうかを決めるのは、Functionの登録の順です。

**registryに残った値は、残ったDeviceではありません。** Windowsは `DeviceInterfaceGUIDs`（とCOMの `PortName`）を書き、消しません。入れ替えの後には古い値が残りますが、アプリと同じ方法（`SetupDiGetClassDevs` に `DIGCF_PRESENT | DIGCF_DEVICEINTERFACE`）で列挙すると、返るのは生きているinterfaceだけでした。

#### identityの各項目

| 項目 | 既定 | 変えたとき |
| --- | --- | --- |
| `vid`、`pid` | `0x303A`、`0x4000` | 新しいinstance。driverもGUIDも新しく記録される |
| `serialNumber` | なし | 同じく新しいinstance。出さなければinstanceはportの経路 |
| `deviceVersion`（bcdDevice） | `0x0100` | 次の接続でhardware IDが `...&REV_0200` などに変わる。instanceとdriverは同じ |
| `product` | `"EspUsbDevice"` | busが報告する説明は新しい文字列。デバイスマネージャーの表示名は、driverを入れたときに決まり、Windowsが再構成するまで変わらない |
| `manufacturer` | `"EspUsbDevice"` | 見える変化なし。Windowsの製造元の欄はdriverのINF由来 |
| `deviceInterfaceGuid` | ライブラリの既定 | revisionが自動なら次の接続で反映、固定なら据え置き。製品では自分のGUIDを付ける |
| `msOs20VendorRevision` | 0（自動） | GUIDの変更を効かなくできる唯一の項目 |
| `msOs20Layout` | Auto | **interfaceが1つの機器に `Subsets` を強制すると、driverが割り当たらない（Code 28）。** Autoが正しい形を選ぶ |
| `msOs20CcgpDevice` | オフ | 親が `usbccgp`、子 `MI_00` が `WINUSB`。GUIDは子だけに付く |
| `webusbEnabled`、`webusbUrl` | オフ | Windows側には何も起きない。読むのはbrowser |
| `maxPowerMilliamps`、`selfPowered` | 100、false | configuration descriptorでしか見えない |
| Function名（CDCのconstructorの `name` など） | なし | 複合機器の子の説明がその名前になる。なければ `product` |

#### 自分で測る方法

Windowsの挙動はbuildで変わります。製品で上の表のどれかに頼る前に、対象のWindowsで測り直し、buildを書き留めてください（`winver`、またはregistryの `DisplayVersion` と `CurrentBuild`.`UBR`）。

1. **1回の書込みで1つだけ変える。** 「すでにこのDeviceに会ったPCで何が起きるか」を見たいなら、同じPIDを使う。出荷済みの製品で大事なのはこちらです。
2. **DeviceをWSLにattachしない。** usbipdでattachしたDeviceは、WindowsからはUSBIPのDeviceで、何も割り当てられません。
3. **Windowsを先に読み、ボードは後に読む。** UARTアダプタによっては、portを開くとボードがresetされ、再接続が起きます。
4. **`Get-PnpDevice` の `OK` を信じない。** 開始待ちや開始失敗の子にもOKを返しました。イベントビューアーの `Microsoft-Windows-Kernel-PnP/Configuration` を読みます（400＝構成済み、410＝開始、411＝開始失敗、430＝追加のinstallが必要）。WinUSBの割当ては `setupapi.dev.log` に記録されないことがあります。
5. **driverだけでなくdevice interfaceを確かめる。** `pnputil /enum-interfaces /class {GUID}` と、アプリと同じ列挙を使います。
6. **Windowsが保持した値を読む。** instanceの `Device Parameters`（`DeviceInterfaceGUIDs`、`PortName`）を、Deviceが送ったものと比べます。

v2の試験道具 [`windows_identity`](https://github.com/tanakamasayuki/EspUsbDevice/tree/3fed50d0d777f600432b71381faccd93adb9a202/tests/manual/windows_identity/) のPC側scriptは、VID/PIDに一致する全nodeについて4〜6をWSLのshellから1回で行います。同じdirectoryのスケッチはv2のAPIで書かれているので、v3では使えません。

### 6.3 macOS

- **システム情報 → USB**：treeとdescriptorの要約
- `ioreg -p IOUSB -l -w 0`：より詳しい情報
- Wiresharkの `XHC20` interfaceで記録

v3はmacOSでは未確認です。

### 6.4 別のESP32をHostにする

対象がPCではなく組込み機器なら、[EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost) を載せたESP32をHost役にできます。両側のログをPCから同時に読めるので、切り分けが速くなります。このリポジトリの試験（[validation](../validation/README.ja.md)）もこの構成です。

### 6.5 動いている機器と比べる

自作のDeviceがHostに嫌われるときは、**同じclassの市販品のdescriptorと並べて見る**のが近道です。

```sh
lsusb -v -d <市販品のvid:pid> > known-good.txt
lsusb -v -d 303a:4051         > mine.txt
diff -u known-good.txt mine.txt
```

interfaceの並び、IADの有無、`bInterfaceProtocol`、endpointのintervalなど、「規格では任意だが、実際のHostのdriverが前提にしている」項目が差に出ます。

---

## 7. トラブルシューティング

症状から引ける対処の一覧は[troubleshooting.ja.md](troubleshooting.ja.md)にあります。ここには主なものだけを載せます。

| 症状 | ありがちな原因 | 確かめること・対処 |
| --- | --- | --- |
| `begin()` が `ESP_ERR_INVALID_SIZE` | endpointかFIFOの予算を超えた。HSでMSCの `transferBytes` がMPSの倍数でない、など | DescriptorDumpの予算を見る。Functionを減らす。P4ならHSにする（[4.3](#43-hsで変わる値)、[4.4](#44-endpointの予算)、[4.5](#45-fifoの予算)） |
| `begin()` が `ESP_ERR_NO_MEM` | heap不足 | 容量を減らす。ringなどをPSRAMに置く（[2.4](#24-容量は実行時に決める)） |
| `begin()` が `ESP_ERR_NOT_SUPPORTED` | USB OTGがないchip、またはないcontrollerを指定した | S2／S3／P4か、`config.controller` を確かめる |
| `begin()` が `ESP_ERR_INVALID_STATE` | 二重の `begin()`、動作中に追加したFunctionが残っている | `end()` してから構成し直す（[2.2](#22-configurebeginend)） |
| `end()` が `ESP_ERR_TIMEOUT` | callbackが戻らず、workerが `stopTimeoutMs` 内に止まらない | callbackを短くする。戻った後にもう一度 `end()` を呼ぶ（[2.2](#22-configurebeginend)） |
| `begin()` は成功するがHostに何も出ない | コネクタが違う（UART側、Serial/JTAG側） | 回路図で確かめる（[1.2](#12-どのコネクタがデバイス側か)） |
| 同上 | S3の `USB Mode` がHardware CDC and JTAG | 「USB-OTG (TinyUSB)」にする（[4.7](#47-arduino-esp32の標準usbとは同時に使えない)） |
| 同上 | 充電専用のケーブル | データ線入りに替える |
| 同上（P4） | controllerとコネクタが合っていない | `config.controller` を、挿したコネクタ（HS／FS）に合わせる（[4.2](#42-esp32-p4のhsとfsの選び方)） |
| P4のHS controllerなのに12 Mbpsで列挙される | HostかhubがFS | `device.speed()` が `Full` かを見る。意図どおりなら問題ない。HSが要るならHSのportにつなぐ |
| Windowsで「デバイス記述子要求の失敗」 | descriptorが不正、または返せていない | USB Device Tree Viewerで生のbyteを見て、DescriptorDumpと比べる |
| 変えたのにHostの表示が古いまま | Windowsが保持している値 | 何が保持されるかを[6.2](#62-windows)で確かめる。白紙にするならPIDかserialを変える |
| SET_CONFIGURATIONまで進むがdriverが載らない | class／subclass／protocolの組合せ | 市販の同種機器と差を取る（[6.5](#65-動いている機器と比べる)） |
| VendorのinterfaceをWindowsで開けない | WinUSBが割り当てられていない | Microsoft OS 2.0 descriptorを確かめる。`msOs20Layout` をAutoに戻す |
| HIDのキー入力が化ける | Host側のキーボード配列と合っていない | `keyboard.setLayout()` をHostの配列に合わせる |
| BIOS／UEFIでキーボードが効かない | boot protocolに切り替わっている | Consoleで `HOST_HID_PROTOCOL` を確かめる。NKROはbootでは6キーになる |
| 送ったのにHostに届かない | `ready()` になる前に送っている | `device.ready()` を確かめてから送る。戻り値も確かめる |
| ログが出ない、`begin()` の直後に切れる | コネクタが1つのボードで、ログとDeviceが同じ口 | 2口のボードを使う。またはHost役のESP32を使う（[3.3](#33-開発中のコネクタの使い方)） |
| 抜き挿しを繰り返すと動かなくなる | 前の接続の状態が残っている | `end()`／`begin()` の繰返しで再現させる。VBUS検出がないボードでは、抜去は次のbus resetまで見えない |
| HSでMSCが `begin()` で失敗する | `transferBytes` が512の倍数でない | 512の倍数にする（[4.3](#43-hsで変わる値)） |
| 原因がまったくわからない | Host側のログを見ていない | `dmesg -w`（Linux）、USB Device Tree Viewer（Windows）を先に見る（[6章](#6-ホストosから自分を観測する)） |

---

## 8. ツール一覧

### 利用者向け（examples/Info/）

| ツール | 用途 |
| --- | --- |
| [`EspUsbDeviceBringUpCheck`](../examples/Info/EspUsbDeviceBringUpCheck/README.ja.md) | **最初に動かす。** `begin()` の成否、列挙、休止、Host→Deviceの疎通。列挙されないときのchecklist付き |
| [`EspUsbDeviceDescriptorDump`](../examples/Info/EspUsbDeviceDescriptorDump/README.ja.md) | **次に動かす。** ライブラリが作ったdescriptorの全部と、endpointの予算。Host不要 |
| [`EspUsbDeviceConsole`](../examples/Info/EspUsbDeviceConsole/README.ja.md) | **詰めるとき。** シリアルから手でHID reportやvendor転送を送り、Hostからの要求を全部表示する |

機能別の例は[examples](../examples/README.md)を見てください。

### Host側の道具

- Linux：`lsusb`、`dmesg`、`usbhid-dump`、`evtest`、Wireshark（usbmon）
- Windows：USB Device Tree Viewer、デバイスマネージャー、イベントビューアー、`pnputil`
- v2の試験道具（[v2 `tests/manual/`](https://github.com/tanakamasayuki/EspUsbDevice/tree/3fed50d0d777f600432b71381faccd93adb9a202/tests/manual/)）：PC側のPyUSB scriptは標準descriptorを読めます。同梱のスケッチはv2のAPIなので、v3では使えません。

---

## 9. 確認済みの範囲

v3の試験は[validation](../validation/README.ja.md)にあります。examplesとは別の試験用スケッチで、`--combo all` で6組の結線、200件を超える試験を実行します。

| 環境 | 確認したこと |
| --- | --- |
| ESP32-S3 2台（FS） | 主な検証環境。全classと共通coreの試験 |
| ESP32-P4 1台のHS controller↔FS controller（linkはFS） | 10 classの基本通信、両controller、再開始、PSRAM上の保存先 |
| 旧P4とP4XのHS直結（480 Mbps、両方向） | S3の試験をHSへ広げたものと、HS用の試験 |
| P4XとLinux PC（HS） | MSC、DFUなどの通信をpyusbで確認 |

未確認のもの：

- ESP32-S2の実機（compileのみ）
- ESP32-S31（未対応）
- Windows、macOSでのv3のclass動作とdriverの割当て
- 実際にケーブルを抜いたときのVBUS検出（`vbusSensing = true`）

---

## 10. 参考資料

- [USB 2.0 Specification](https://www.usb.org/document-library/usb-20-specification)：規格の本体
- [USB Class Codes](https://www.usb.org/defined-class-codes)：class codeの一覧
- [USB Device Class Documents](https://www.usb.org/documents)：HID、CDC、MSC、CCID、UACなど各classの仕様
- [HID Usage Tables](https://usb.org/document-library/hid-usage-tables-16)：HIDのusage一覧。report descriptorを書くときに要る
- [USB Made Simple](https://www.usbmadesimple.co.uk/)：初学者向けの解説
- [USB Device Tree Viewer](https://www.uwe-sieber.de/usbtreeview_e.html)：Windowsでの観測
- [pid.codes](https://pid.codes/)：open sourceプロジェクト向けのVID/PID
- [ESP32-P4 chip revision v3.x User Guide](https://documentation.espressif.com/esp32-p4-chip-revision-v3.x_user_guide_en.html)：旧P4とP4Xの違い
- このリポジトリの[README](../README.ja.md)：導入と対応状況
- [USB Device開発ガイド（上級編）](usb-device-advanced.ja.md)：内部の仕組み、descriptorの構造、callbackの文脈
- [サンプルの進め方](example-workflow.ja.md)：例の試し方と読み方
- [v2からの移行](migrating-from-v2.ja.md)、[Arduino-ESP32のUSBからの移行](migrating-from-arduino-esp32-usb.ja.md)
- [USB経由のfirmware更新](ota-over-usb.ja.md)：chipごとのboot mode、スケッチからの入り方、OTAの経路
- [EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost)：ESP32をHost側にするライブラリ。[USB Host開発ガイド](https://github.com/tanakamasayuki/EspUsbHost/blob/main/docs/usb-host-guide.ja.md)
