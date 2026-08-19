# USB Device開発ガイド

> English: [usb-device-guide.md](usb-device-guide.md)

ESP32を「USB機器そのもの」にして、PCや他のホストに認識させるまでの手引きです。前半はUSB Deviceの基礎、後半はESP32シリーズ固有の注意点と、実際に手を動かす実験手順です。

USBを触るのが初めてでも読めるように書いていますが、「なぜホストが認識してくれないのか」を自分で切り分けられるようになることを目標にしているので、原理の説明は省いていません。急ぐ場合は [実験の進め方](#4-実験の進め方) から読み、わからない用語が出てきたら前半に戻ってください。

動かした後で、なぜそう動くのか・限界はどこかを知りたくなったら [USB Device開発ガイド（上級編）](usb-device-advanced.ja.md) へ進んでください。TinyUSBとの関係、ディスクリプタのバイト単位の構造、コールバックのコンテキスト、独自クラスの実装を扱います。

USB機器をESP32に**つなぐ**側（ESP32がホストになる）を探している場合は、[EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost) とその [USB Host開発ガイド](https://github.com/tanakamasayuki/EspUsbHost/blob/main/docs/usb-host-guide.ja.md) が対になります。

## 目次

1. [USB Deviceの基礎](#1-usb-deviceの基礎)
2. [電源とコネクタ](#2-電源とコネクタ)
3. [ESP32シリーズ固有の注意点](#3-esp32シリーズ固有の注意点)
4. [実験の進め方](#4-実験の進め方)
5. [ホストOSから自分を観測する](#5-ホストosから自分を観測する)
6. [トラブルシューティング](#6-トラブルシューティング)
7. [ツール一覧](#7-ツール一覧)
8. [参考資料](#8-参考資料)

---

## 1. USB Deviceの基礎

### 1.1 ホストとデバイス

USBは対等な通信ではありません。**ホストが1台、デバイスがぶら下がる**構造で、通信は必ずホストが起点になります。デバイスが自発的にデータを送りつけることはできず、ホストが「何かある?」と聞きに来て初めてデータが返ります。

このライブラリを使うとき、ESP32は**デバイス**側です。つまり、

- **こちらから通信を始めることはできません。** `keyboard.write()` が実際にバスへ出るのは、ホストがそのエンドポイントをポーリングした瞬間です。ホストが見に来なければ、送信は待たされるか捨てられます。
- **自分が何者かはすべて自己申告です。** キーボードに見えるかシリアルポートに見えるかは、こちらが返すディスクリプタで決まります。
- **ホストの都合で切られます。** サスペンド、バスリセット、コンフィグレーション解除はいつでも起き、そのたびにこちらの状態は初期化されます。

Host側ライブラリとの一番大きな違いはここです。Host側の仕事が「相手のディスクリプタを読んで理解すること」なら、Device側の仕事は「**ホストに理解されるディスクリプタを書くこと**」です。

### 1.2 どのコネクタがデバイス側か

ESP32ボードのUSB Type-Cコネクタは、同じ形でも役割が違います。

- **USB-UARTブリッジのコネクタ**（CP2102、CH340などのチップにつながっている。書き込みとシリアルモニタ用）
- **ESP32のUSB OTGコネクタ**（SoCのD+/D-に直結。**このライブラリが使うのはこちら**）
- **USB Serial/JTAGのコネクタ**（S3/P4のSoC内蔵ペリフェラル。書き込みとログにも使える）

シルク印刷の`USB`/`OTG`/`UART`は当てになりません。**回路図で確認してください。**

開発中は**USBコネクタが2つあるボード**を強く推奨します。ログを見る口と、ホストにつなぐ口を分けられるからです。コネクタが1つのボードでは、USB機器として列挙した瞬間にシリアルモニタが消えるため、切り分けが極端に難しくなります。

### 1.3 速度

| 速度 | 略称 | 転送レート | 備考 |
|------|------|-----------|------|
| Low Speed | LS | 1.5 Mbps | このライブラリでは扱いません |
| Full Speed | FS | 12 Mbps | ESP32-S2/S3、ESP32-P4のFS controller |
| High Speed | HS | 480 Mbps | ESP32-P4のHS controllerのみ |
| SuperSpeed | SS | 5 Gbps以上 | ESP32では扱えません |

**速度を決めるのはデバイス側です。** D+/D-のプルアップとチャープでホストに申告します。ただし選べるのは「このボードのcontrollerが持っている速度」だけで、ソフトから任意に変えられるものではありません（[3.2](#32-esp32-p4のfshs選択)）。

HighSpeedで動く場合、ホストは「FullSpeedにつないだらどうなるか」も聞いてきます。これに答えるのが **Device Qualifier** と **Other Speed Configuration** で、このライブラリが自動生成します。バルクエンドポイントの最大パケットサイズはHSで512、FSで64になるため、同じ構成でも2種類のディスクリプタが必要になるからです。

### 1.4 列挙（enumeration）— 答える側から見た流れ

ホストがデバイスを使えるようになるまでの手順です。**どこで止まったか**が切り分けの出発点になります。

1. **接続検出** — こちらがD+をプルアップして「FS/HSデバイスがいます」と申告する
2. **バスリセット** — ホストがバスをリセットする
3. **アドレス割り当て** — `SET_ADDRESS` に応答する
4. **デバイスディスクリプタ要求** — `GET_DESCRIPTOR(DEVICE)` に18バイトを返す
5. **コンフィグレーションディスクリプタ要求** — 構成全体を返す。ここが一番失敗しやすい
6. **文字列ディスクリプタ要求** — 製造者名、製品名、シリアル番号
7. **`SET_CONFIGURATION`** — ここで初めてクラスのエンドポイントが開く
8. **ホスト側ドライバのバインド** — OSがクラスコードを見てドライバを載せる

`device.ready()` が `true` になるのは7が終わったあとです（内部的には `tud_mounted()`）。

切り分けの要点:

- **1〜3で止まる**（ホスト側に何も出ない）→ 電気的な問題。ケーブル、コネクタ、ビルド設定。
- **4〜6で止まる**（ホストが「デバイス記述子要求の失敗」を出す）→ ディスクリプタの問題。
- **7まで行くがドライバが載らない**（不明なデバイスとして出る）→ クラスコードやOS側ドライバの問題。
- **8まで行くが動かない** → クラス固有のプロトコルの問題。

### 1.5 ディスクリプタは自分で書く

デバイスは自分の構成を**ディスクリプタ**という構造体の列で申告します。

```
Device（VID/PID、USBバージョン、EP0のサイズ）
└── Configuration（消費電流、バスパワー/セルフパワー）
    ├── (IAD: 複数インターフェースを1機能としてまとめる)
    ├── Interface 0（クラス／サブクラス／プロトコル ← 「何であるか」はここ）
    │   ├── クラス固有ディスクリプタ（HIDディスクリプタ、CDC機能ディスクリプタ など）
    │   ├── Endpoint 0x81（IN, interrupt, 最大パケット8, interval 10）
    │   └── Endpoint 0x01（OUT, ...）
    └── Interface 1
        └── ...
```

`EspUsbDevice` では、これを直接書く必要はありません。登録したクラスから `begin()` の中で組み立てられます。ただし**何が組み立てられたかは必ず確認してください**。[`examples/Info/EspUsbDeviceDescriptorDump`](../examples/Info/EspUsbDeviceDescriptorDump/) がその中身を全部表示します。

押さえておく点:

- **クラスはデバイスではなくインターフェースに付く。** このライブラリはデバイスディスクリプタのクラスを常に `0x00`（インターフェース側で決まる）にするので、**「何であるか」はインターフェースを見ないとわかりません**。複数インターフェースを1機能としてまとめるIAD（`bDescriptorType=0x0b`）は、CDCを含む構成でコンフィグレーションディスクリプタの中に現れます。
- **インターフェース番号とエンドポイント番号はライブラリが一括採番する。** 手で決めるものではありません。実際の番号はDescriptorDumpで確認します。
- **HIDのレポートディスクリプタは別物。** コンフィグレーションディスクリプタの中には「レポートディスクリプタが何バイトあるか」しか書かれておらず、中身はホストが別要求で取りに来ます。HIDデバイスの入力の意味を決めているのはこちらです。
- **複合HIDは1つのインターフェースにまとめられる。** キーボード＋マウス＋ゲームパッドは、インターフェースを増やすのではなく、1つのHIDインターフェース上でレポートIDで区別します（キーボード=1、マウス=2、ゲームパッド=3、コンシューマ=4、システム=5、ベンダー=6）。

### 1.6 エンドポイントと転送タイプ

エンドポイントは通信の口です。アドレス（`0x81` のようにbit7が方向、下位4bitが番号）、方向、転送タイプ、最大パケットサイズ（MPS）、ポーリング間隔を持ちます。

| 転送タイプ | 特徴 | このライブラリでの用途 |
|-----------|------|----------------------|
| **Control** | EP0で行う設定用の転送。全デバイスが必ず持つ | 列挙、クラス要求、HIDのSET_REPORT、vendor control |
| **Interrupt** | ホストが一定間隔でポーリングする。少量・低遅延 | HID、CDCの通知、CCIDの状態通知 |
| **Bulk** | 大量データ。帯域保証なし、エラー再送あり | CDCデータ、MSC、NCM、USBVendor |
| **Isochronous** | 一定帯域を予約。エラー再送なし | USB Audio |

方向は**ホストから見た向き**です。「IN」はデバイス（自分）からホストへデータが行きます。

Host側では、エンドポイントはホストのチャネルという資源を消費しました。Device側では、**エンドポイントはこのSoCのUSB controllerが持つ本数を消費します**。ここがDevice側で最初にぶつかる壁です（[3.3](#33-endpoint予算)）。

### 1.7 何に「見せるか」を選ぶ

Host側では「この機器はどう見えるか」は相手が決めることでした。Device側では**こちらが選べます**。これはDevice開発でいちばん重要な設計判断です。

| 見せ方 | クラス | 向いている用途 | ホスト側のドライバ |
|--------|--------|--------------|------------------|
| HIDキーボード / マウス / ゲームパッド | `0x03` | 入力デバイス、マクロパッド、自動操作 | OS標準。**ドライバ不要** |
| HID（ベンダー独自レポート） | `0x03` | 独自データの双方向。Windowsでもドライバ不要 | OS標準（HIDAPI等でアクセス） |
| CDC ACM | `0x02` | シリアルポートとして見せる。既存のシリアルアプリが使える | OS標準（Windows 10以降は inbox） |
| USB MIDI | `0x01`/MIDI | DAW、シンセ、コントローラ | OS標準。**ドライバ不要** |
| Mass Storage | `0x08` | ドライブとして見せる。ファイル受け渡し、設定ファイル | OS標準 |
| USB Audio | `0x01` | スピーカー、マイク | OS標準 |
| CDC-NCM | `0x02`/NCM | ネットワークアダプタとして見せる。Web UIをUSB越しに | 最近のOSは標準対応 |
| CCID | `0x0b` | スマートカードリーダー | OS標準（PC/SC） |
| Vendor Specific | `0xff` | 独自プロトコル、最高速のバルク転送 | **Windowsではドライバ指定が必要**（WinUSB / WebUSB） |

判断基準は単純です。

1. **ホスト側にドライバを入れさせたくないなら、標準クラスにする。** HID、CDC、MIDI、MSCはどのOSでもそのまま動きます。
2. **独自データを流したいが、ドライバも入れたくないなら、HIDのベンダー独自レポートにする。** これは実際によく使われる手で、市販の機器も多くがこの形です。速度は出ませんが、権限もドライバもなしで双方向に通信できます。
3. **帯域が必要なら Vendor Specific（bulk）。** ただしWindowsでは、WinUSBにバインドさせるためにMicrosoft OS 2.0ディスクリプタが必要になります。このライブラリはWebUSBを有効にすると自動で返します。
4. **既存のアプリに食わせたいなら、そのアプリが期待するクラスにする。** DAWならMIDI、ターミナルソフトならCDC。

複数を組み合わせた複合デバイスも作れますが、エンドポイント予算とホストOSの都合が絡むので、まず単機能で動かしてから広げてください（[Step 7](#step-7-複合デバイスへ広げる)）。

---

## 2. 電源とコネクタ

### 2.1 デバイスは電源をもらう側

Host側では「VBUSを出せるか」が最初の関門でした。Device側では逆で、**ホストからVBUSをもらいます**。したがって電源そのものでつまずくことは少ないのですが、次は起きます。

- **ボードがVBUSから給電されない配線になっている。** OTGコネクタのVBUSがSoCの電源につながっていないボードでは、そのコネクタだけを挿しても起動しません。ログ用のコネクタから別途給電してください。
- **セルフパワーで動かすときの申告。** 別電源で動くなら `config.selfPowered = true` にします。これはディスクリプタ上の申告であり、実際の配線と一致させる意味しかありませんが、ホストの電力管理が見ています。
- **VBUS検出がないボードでは、抜線に気づけない。** 多くのESP32ボードはVBUSセンシングを配線していないため、抜線が `onBusDetached()` に届かず、次に挿したときのバスリセットで初めて状態が更新されます。このため、ライブラリは `onBusAttached()`（`SET_CONFIGURATION`）側でも状態を初期化します。

### 2.2 bMaxPower

`config.maxPowerMilliamps`（既定100mA）は、バスから取ると申告する電流です。実際の消費と一致している必要はありませんが、Wi-Fiを使うなど消費が大きい構成では500mAまで上げるのが素直です。ホストによっては申告を超える消費でポートを落とします。

### 2.3 開発中のコネクタ構成

推奨は次の形です。

| 用途 | つなぐ先 |
|------|---------|
| 書き込み・シリアルログ | USB-UARTブリッジのコネクタ、またはUSB Serial/JTAG |
| 検証対象のホスト | ESP32のUSB OTGコネクタ |

コネクタが1つしかないボード（AtomS3など）を最終製品にするのは構いませんが、**開発中は2口のボードを使ってください**。列挙した瞬間にログが切れると、`MOUNTED` すら読めません。

もう一つの手は、ホスト役に別のESP32（[EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost)）を使うことです。このリポジトリの [`tests/peer`](../tests/peer/) はその構成で、両側のログをPCから同時に読めます。

---

## 3. ESP32シリーズ固有の注意点

### 3.1 対応チップとcontroller

| チップ | USB device能力 | `EspUsbController::Auto` の解決先 |
|--------|---------------|--------------------------------|
| ESP32-S2 | FSのdevice controller 1つ | FullSpeed |
| ESP32-S3 | FSのdevice controller 1つ。主対象 | FullSpeed |
| ESP32-P4 | FS（rhport 0）とHS（rhport 1）の2つ | **HighSpeed** |
| ESP32（無印）, C3, C6 など | USB OTGを持たない。**このライブラリは動きません** | — |

必要なArduino-ESP32コアは **3.3.9以上**です。各バージョンのビルド結果は [`docs/`](.) の `COMPATIBILITY.<version>.md` にあります。

### 3.2 ESP32-P4のFS/HS選択

`config.controller` で選びます。既定の `Auto` はP4では**HighSpeed**になります。

| | FullSpeed（rhport 0） | HighSpeed（rhport 1） |
|---|---|---|
| PHY | SoC内蔵 | 外部UTMI PHY |
| 既定ピン | GPIO26=D-, GPIO27=D+ | ボード上のUTMI配線 |
| 速度 | 12 Mbps | 480 Mbps |
| endpoint予算 | 狭い（[3.3](#33-endpoint予算)） | 広い |

どのコネクタがどちらに配線されているかは**ボードごとに違います**。回路図で確認してください。USB Serial/JTAGのコネクタでもGPIO26/27のFSペアでもない第3のコネクタがHS用、という構成が普通です。

P4のFS PHYはUSB Serial/JTAGとピンを共有しており、`usb_wrap_ll_phy_select(&USB_WRAP, 0)` でGPIO24/25側へ切り替えられます（[`examples/P4FullSpeedDevice`](../examples/P4FullSpeedDevice/) にコメントアウトで記載）。切り替えるとUSB Serial/JTAGがGPIO26/27へ移るため、そちらのシリアルモニタは切れます。

### 3.3 endpoint予算

**Device側で最初にぶつかる制限です。** USB controllerが持てるエンドポイントの本数と番号には上限があり、超える構成は `begin()` がPHY起動前に `ESP_ERR_INVALID_SIZE` で拒否します。

| controller | endpoint番号の上限 | control以外のIN | control以外のOUT |
|---|---|---|---|
| ESP32-S2 / S3 | 5 | 4 | 5 |
| ESP32-P4 rhport 0（FS） | 6 | 4 | 6 |
| ESP32-P4 rhport 1（HS） | 15 | 7 | 15 |

クラスごとの消費（おおよそ）:

| クラス | 消費するendpoint |
|--------|----------------|
| HID（複合HID全体で1インターフェース） | IN 1本 |
| CDC ACM | 通知用 IN 1本 + データ IN/OUT 各1本 |
| USB MIDI | IN/OUT 各1本 |
| MSC | IN/OUT 各1本 |
| USBVendor | IN/OUT 各1本 |
| USB Audio | 方向ごとに isochronous 1本 |
| CDC-NCM | 通知用 IN 1本 + データ IN/OUT 各1本 |
| CCID | IN/OUT 各1本 + 通知用 IN 1本 |

ESP32-S3で**IN方向が4本まで**というのが実際の効き方です。HID + CDC + MSC で IN は 1+2+1 = 4本になり、ここが上限です。もう1つクラスを足すと入りません。

確認する方法は簡単で、[`EspUsbDeviceDescriptorDump`](../examples/Info/EspUsbDeviceDescriptorDump/) の `DUMP_ENABLE_*` を目的の構成にしてビルドし、末尾の endpoint budget を読むだけです。実機にホストをつなぐ必要すらありません。

### 3.4 その他のライブラリ側の上限

| 項目 | 上限 | 超えたときの挙動 |
|------|------|----------------|
| 登録できるクラス数 | 4 | 5個目は constructor 内の `addClass()` が失敗し、**黙って登録されない** |
| コンフィグレーションディスクリプタ | 704バイト | `begin()` が失敗する |
| HIDレポートディスクリプタ | 256バイト | 同上 |
| 文字列ディスクリプタ | 63文字 | 切り詰められる |

クラス数の上限は静かに効くので、複合デバイスを組むときは DescriptorDump のインターフェース一覧を必ず確認してください。

### 3.5 Arduino-ESP32標準USBスタックとは排他

このライブラリは自前の TinyUSB をビルドし、ESP-IDFのPHY/controllerを直接初期化します。したがって、

- **`USB.begin()` を呼ばないでください。** `USBHIDKeyboard`、`USBHIDMouse`、`USBCDC` も併用できません。
- **ビルド時の USB Mode は「USB-OTG (TinyUSB)」にしてください。** ESP32-S3/P4 のボードメニューにある `USB Mode` を `Hardware CDC and JTAG` にすると、D+/D-がUSB Serial/JTAGペリフェラルへ回り、OTG controllerは使えません。`arduino-cli` では `esp32:esp32:esp32s3:USBMode=default` が「USB-OTG (TinyUSB)」です（このリポジトリの `sketch.yaml` はすべてこれを指定しています）。
- **`USB CDC On Boot` は無効のままにしてください。** 有効にすると Arduino 側が別の USB CDC を立てようとします。

この構成では `Serial` はUART側に出ます。ログを見る口を別に用意する必要があるのはこのためです（[2.3](#23-開発中のコネクタ構成)）。

### 3.6 ログ

列挙に失敗する原因は、ESP-IDF側のログにしか出ないことがあります。調査時はArduino IDEの Core Debug Level を `Verbose` にしてください。

同時に、**ホストOS側のログも必ず見てください**（[5章](#5-ホストosから自分を観測する)）。Device側の開発では、ホストが何を受け取って何を嫌がったかが最大の情報源です。

---

## 4. 実験の進め方

新しいUSBデバイスを作るまでの手順です。**上から順に、飛ばさずに**進めてください。多くの失敗は、後段の工程で前段の問題（コネクタ、ビルド設定、endpoint予算）を悩んでいるために起きます。

### Step 0. 机の上の確認（ソフトを書く前）

- 回路図で、**どのコネクタがESP32のUSB OTGに直結しているか**を確認する
- ログ用のコネクタと、ホストにつなぐコネクタを分けられるか確認する
- 使うケーブルがデータ線入りか（充電専用でないか）確認する
- ボードメニューの `USB Mode` が「USB-OTG (TinyUSB)」であることを確認する

### Step 1. 起動と列挙を確認する

[`examples/Info/EspUsbDeviceBringUpCheck`](../examples/Info/EspUsbDeviceBringUpCheck/) を書き込みます。HIDキーボードとして列挙しますが、キーは一切送らないので、PCに挿したままで安全です。

- `BEGIN ok` が出るか → 出なければボード側の問題（ターゲット、コア版、endpoint予算）
- ホストにつないで `MOUNTED` が出るか → 出なければ電気的な問題かディスクリプタの問題
- 速度（Full / High）を控える
- ホスト側でCapsLockを押して `HOST_OUTPUT_REPORT` が出るか → 双方向の疎通確認

ここまで通れば、ボードとビルド設定は正常です。以降の問題は自分の構成に固有だと切り分けられます。

### Step 2. 何に見せるかを決める

[1.7の表](#17-何に見せるかを選ぶ)から、目的に合ったクラスを選びます。この選択がプロジェクト全体を決めるので、先に決めてください。迷ったときの原則:

- ホストにドライバを入れさせたくない → 標準クラス
- 独自データ＋ドライバ不要 → HIDのベンダー独自レポート
- 帯域が要る → Vendor Specific（bulk）
- 既存アプリに食わせたい → そのアプリのクラス

### Step 3. クラスAPIで最小構成を動かす

選んだクラスに対応するサンプルを、そのまま書き込みます。

| クラス | サンプル |
|--------|---------|
| HID keyboard / mouse | [`Keyboard`](../examples/Keyboard/) / [`Mouse`](../examples/Mouse/) |
| HID gamepad / メディアキー | [`Gamepad`](../examples/Gamepad/) / [`MediaKeys`](../examples/MediaKeys/) |
| HID 独自レポート | [`CustomHID`](../examples/CustomHID/) / [`VendorHID`](../examples/VendorHID/) |
| CDC ACM | [`Serial`](../examples/Serial/) |
| USB MIDI | [`MIDI`](../examples/MIDI/) |
| MSC | [`MSC`](../examples/MSC/) / [`MSCFatRamDisk`](../examples/MSCFatRamDisk/) / [`MSCSdCard`](../examples/MSCSdCard/) |
| USB Audio | [`AudioSpeaker`](../examples/AudioSpeaker/) / [`AudioMicrophone`](../examples/AudioMicrophone/) / [`AudioHeadset`](../examples/AudioHeadset/) |
| CDC-NCM | [`UsbNetwork`](../examples/UsbNetwork/) |
| CCID | [`SmartCardReader`](../examples/SmartCardReader/) |
| Vendor bulk | [`USBVendor`](../examples/USBVendor/) |

**サンプルが動いてから自分のコードを書いてください。** 動かないときに、ライブラリの問題か自分の構成の問題かを分けられます。

### Step 4. 自分が何を宣言しているか確認する

[`examples/Info/EspUsbDeviceDescriptorDump`](../examples/Info/EspUsbDeviceDescriptorDump/) の `DUMP_ENABLE_*` を自分の構成に合わせてビルドし、シリアルに出る内容を読みます。ホストにつなぐ必要はありません。

確認する点:

- インターフェースはいくつあり、それぞれのクラス／サブクラス／プロトコルは何か
- エンドポイントの種類、方向、MPS、interval
- HIDレポートディスクリプタは何バイトか
- **endpoint予算に収まっているか**

### Step 5. ホストが受け取った内容と突き合わせる

Step 4は「送ったつもりの内容」です。ホストが実際に受け取った内容と一致しているかを確認します。

```sh
# Linux
lsusb -v -d 303a:4051

# どのOSからでも（PyUSB）
cd tests
uv run --with pyusb python manual/device_inspect/device_inspect.py --pid 0x4051
```

一致していれば、ディスクリプタ層は正常です。一致しない、あるいはホスト側に何も出ないなら、問題はもっと下（列挙、電気、controller設定）にあります。詳しい観測方法は[5章](#5-ホストosから自分を観測する)にまとめます。

### Step 6. 手打ちで詰める

クラスは動くが「ホストのソフトが期待どおり反応しない」という段階では、[`examples/Info/EspUsbDeviceConsole`](../examples/Info/EspUsbDeviceConsole/) が効きます。ビルドし直さずに1転送ずつ試せるので、試行のサイクルが速くなります。

```
> state
STATE mounted=1 speed=full leds=0x00 hid_protocol=report vendor_mounted=0
> key 0x04                            # このusageにホストは反応するか
> report 1 00 00 05 00 00 00 00 00    # 生レポートを直接投げる
> vendor 01 02 03 04                  # vendor bulk INに生バイト
```

同時に、ホストから降りてくるもの（HID output report、protocol切り替え、vendor control request、bulk OUT）が `HOST_` 接頭辞で全部出ます。**ホスト側の純正ソフトを動かしながらこれを眺めると、ホストが期待している初期化シーケンスがわかります。**

### Step 7. 複合デバイスへ広げる

単機能で動いたら組み合わせます。ここでの制約は2つだけです。

- **endpoint予算**（[3.3](#33-endpoint予算)）— DescriptorDumpで先に確認する
- **クラス数4個まで**（[3.4](#34-その他のライブラリ側の上限)）

複合HIDはインターフェースが増えるのではなく、1つのHIDインターフェース上でレポートIDが増えます。[`CompositeHidCdcMsc`](../examples/CompositeHidCdcMsc/)、[`KeyboardMouse`](../examples/KeyboardMouse/) が参考になります。

ホストOSによっては、複合デバイスのインターフェースの並び順やIADの有無で挙動が変わります。組み替えたら、対象にするOSで必ず再確認してください。

### Step 8. 耐性を確認する

一度列挙できることと、使い続けられることは別です。

```sh
cd tests
uv run --with pyusb python manual/enumeration_soak/enumeration_soak.py --cycles 50
```

再列挙とコンフィグレーション切り替えを繰り返し、ディスクリプタが変化しないか、途中で応答しなくならないかを確認します。加えて、

- ホストをサスペンド／復帰させる
- ケーブルを何度も抜き挿しする
- ホスト側を再起動して、起動時（BIOS/UEFI）に認識されるか見る（HIDならboot protocolに切り替わります）

### Step 9. 結果を残す

- 動く最小のスケッチを `examples/` 配下に置く（このリポジトリに追加する場合は README.md / README.ja.md / sketch.yaml も揃える）
- 手で確認する必要がある動作は `tests/manual/` に追加する
- 2台構成で自動化できるものは `tests/peer/` に追加する
- **動作を確認したホストOSとそのバージョンを明記する。** Device側では、これがHost側でいうVID/PIDに相当する最重要情報です。

---

## 5. ホストOSから自分を観測する

Host側開発では、相手の機器をキャプチャして解析しました。Device側では、**自分がホストにどう見えているか**を観測します。ホストOSごとに道具が違います。

### 5.1 Linux

```sh
# 何が起きたか（挿す前から流しておく）
sudo dmesg -w
udevadm monitor --udev

# ディスクリプタ全部
lsusb -v -d 303a:4050

# ツリーと速度（12M / 480M）
lsusb -t

# HIDレポートディスクリプタ
sudo usbhid-dump -d 303a:4050

# HIDキーボード/マウスとして何が届いているか
sudo evtest

# 自分宛の転送をキャプチャする
sudo modprobe usbmon
sudo wireshark   # usbmonX を選ぶ
```

`dmesg` は特に重要です。ディスクリプタが不正なら理由が出ますし、どのドライバがバインドしたかもわかります。

### 5.2 Windows

- **デバイスマネージャー** — 「不明なUSBデバイス（デバイス記述子要求の失敗）」はディスクリプタ段階の失敗、「!」付きのCode 10 / Code 43 はドライバのバインド失敗です。
- **[USB Device Tree Viewer](https://www.uwe-sieber.de/usbtreeview_e.html)** — Windowsで一番使える道具です。ディスクリプタの生バイトと、Windowsが出したエラーを両方見られます。
- **イベントビューアー** — ドライバのバインド失敗の詳細。
- **`pnputil /enum-devices /connected`** — バインドされたドライバの確認。

**Windowsは VID/PID ごとにディスクリプタをキャッシュします。** これはDevice側開発で最も多い「なぜか古い名前のまま」「変更が反映されない」の原因です。対策は次のいずれかです。

- 開発中は**ディスクリプタを変えるたびにPIDを変える**（このライブラリのInfoスケッチが別々のPIDを使っているのはこのためです）
- `config.serialNumber` を変える
- デバイスマネージャーで対象デバイスを削除してから挿し直す

### 5.3 macOS

- **システム情報 → USB** — ツリーとディスクリプタの要約
- `ioreg -p IOUSB -l -w 0` — より詳しい情報
- Wireshark + `XHC20` インターフェースでキャプチャ

### 5.4 別のESP32をホストにする

対象がPCではなく組み込み機器なら、[EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost) を載せたESP32をホスト役にできます。両側のログをPCから同時に読めるので、切り分けが速くなります。このリポジトリの [`tests/peer`](../tests/peer/) がその構成です。

### 5.5 うまくいっている機器と比べる

自作デバイスがホストに嫌われるとき、**同じクラスの市販品のディスクリプタと並べて見る**のが最短経路です。

```sh
lsusb -v -d <市販品のvid:pid> > known-good.txt
lsusb -v -d 303a:4051         > mine.txt
diff -u known-good.txt mine.txt
```

インターフェースの並び、IADの有無、bInterfaceProtocol、エンドポイントのintervalなど、「規格上は任意だが、実際のホストドライバが前提にしている」項目が差分に出ます。

---

## 6. トラブルシューティング

この表の拡張版（ビルド・ホストOS・クラス別の問題を含む、症状から引ける対処集）は
[troubleshooting.ja.md](troubleshooting.ja.md) にあります。

| 症状 | ありがちな原因 | 確認・対処 |
|------|--------------|-----------|
| `begin()` が `ESP_ERR_INVALID_SIZE` で失敗 | endpoint予算超過、ディスクリプタが大きすぎる | DescriptorDumpの budget を見る。クラスを減らす。P4ならHSへ（[3.3](#33-endpoint予算)） |
| `begin()` が `ESP_ERR_NOT_SUPPORTED` | 対象チップにUSB deviceがない | S2 / S3 / P4 か確認する |
| `begin()` は成功するがホストに何も出ない | コネクタが違う（UART側／Serial-JTAG側に挿している） | 回路図で確認（[1.2](#12-どのコネクタがデバイス側か)） |
| 同上 | ビルドの `USB Mode` が Hardware CDC and JTAG | 「USB-OTG (TinyUSB)」にする（[3.5](#35-arduino-esp32標準usbスタックとは排他)） |
| 同上 | 充電専用ケーブル | データ線入りに替える |
| 同上（P4） | controllerとコネクタの不一致 | `config.controller` を、挿したコネクタ（FS/HS）に合わせる（[3.2](#32-esp32-p4のfshs選択)） |
| Windowsで「デバイス記述子要求の失敗」 | ディスクリプタが不正、または返せていない | USB Device Tree Viewerで生バイトを見る。DescriptorDumpと突き合わせる |
| 変更したのにホストの表示が古いまま | **Windowsのディスクリプタキャッシュ** | PIDかシリアル番号を変える。デバイスマネージャーから削除して挿し直す（[5.2](#52-windows)） |
| 5個目のクラスが descriptor に出てこない | クラスは4個まで。5個目は黙って落ちる | DescriptorDumpのインターフェース一覧を確認（[3.4](#34-その他のライブラリ側の上限)） |
| `SET_CONFIGURATION` までは行くがドライバが載らない | クラスコード／サブクラス／プロトコルの組み合わせ | 市販の同種機器のディスクリプタと差分を取る（[5.5](#55-うまくいっている機器と比べる)） |
| Vendor interfaceをWindowsで開けない | WinUSBにバインドされていない | `config.webusbEnabled = true` でMS OS 2.0ディスクリプタを返す。それでも駄目ならZadig等でバインドする |
| HIDのキー入力が文字化けする | ホスト側キーボードレイアウトとの不一致 | `keyboard.setLayout()` をホストのレイアウトに合わせる |
| BIOS / UEFIでキーボードが効かない | boot protocolに切り替わっている | Consoleで `HOST_HID_PROTOCOL` を確認。NKROはboot時6キーに折り畳まれる |
| 送信したのにホストに届かない | mount前に送っている | `device.ready()` を確認してから送る。未mount時の送信は捨てられる |
| ログが出ない／`begin()` の直後に切れる | コネクタが1つのボードで、ログと device が同じ口 | 2口のボードを使う。またはpeerボード構成にする（[2.3](#23-開発中のコネクタ構成)） |
| 抜き挿しを繰り返すと動かなくなる | 再列挙時に前回の状態が残っている | `enumeration_soak` で再現させる。`onBusDetached()` / `onBusAttached()` で状態を捨てる |
| MSCがマウントされない | FATイメージ、ブロックサイズ | `MSCFatRamDisk` の動作と比較する |
| Audioが途切れる | FIFOの読み書き周期 | polling APIを一定周期で回す。stream statsを見る |
| 原因がまったくわからない | ホスト側のログを見ていない | `dmesg -w`（Linux）／USB Device Tree Viewer（Windows）を先に見る（[5章](#5-ホストosから自分を観測する)） |

---

## 7. ツール一覧

### 利用者向け（examples/Info/）

| ツール | 用途 |
|--------|------|
| [`EspUsbDeviceBringUpCheck`](../examples/Info/EspUsbDeviceBringUpCheck/) | **最初に動かす。** begin()の成否、列挙されるか、速度、host→device疎通。列挙されない場合のチェックリスト付き |
| [`EspUsbDeviceDescriptorDump`](../examples/Info/EspUsbDeviceDescriptorDump/) | **次に動かす。** ライブラリが組み立てたディスクリプタ全部と、endpoint予算。ホスト接続不要 |
| [`EspUsbDeviceConsole`](../examples/Info/EspUsbDeviceConsole/) | **詰めるとき。** シリアルから手打ちでHID report / vendor転送を送り、ホストからの要求を全部表示する |

機能別のサンプルは [`examples/README.ja.md`](../examples/README.ja.md) を参照してください。
ライブラリ内部の仕組みは [USB Device開発ガイド（上級編）](usb-device-advanced.ja.md) にあります。

### 開発者向け（tests/manual/）

PC側で実行するPyUSBスクリプトです。実行方法は [tests/manual/README.ja.md](../tests/manual/README.ja.md) を参照してください。

| ツール | 用途 |
|--------|------|
| [`device_inspect`](../tests/manual/device_inspect/) | ホストが受け取ったディスクリプタを全部表示。DescriptorDumpとの突き合わせ用。`--json` で差分も取れる |
| [`enumeration_soak`](../tests/manual/enumeration_soak/) | 再列挙とコンフィグレーション切り替えを繰り返し、ディスクリプタが変化しないか確認する |
| [`p4_hs_bulk`](../tests/manual/p4_hs_bulk/) | ESP32-P4のHigh Speed動作と、bulkの実効スループット測定 |
| [`usb_ncm`](../tests/manual/usb_ncm/) | CDC-NCMネットワークデバイスがホストOSにバインドされるか |

自動テストは [`tests/peer`](../tests/peer/)（ESP32 2台構成）と [`tests/unit`](../tests/unit/)（ホスト上のディスクリプタ検証）にあります。構造は [tests/TEST_PLAN.ja.md](../tests/TEST_PLAN.ja.md) を参照してください。

---

## 8. 参考資料

- [USB 2.0 Specification](https://www.usb.org/document-library/usb-20-specification) — 規格本体
- [USB Class Codes](https://www.usb.org/defined-class-codes) — クラスコードの一覧
- [USB Device Class Documents](https://www.usb.org/documents) — HID、CDC、MSC、CCID、UACなど各クラスの仕様
- [HID Usage Tables](https://usb.org/document-library/hid-usage-tables-16) — HIDのusage一覧。レポートディスクリプタを書くとき必須
- [USB Made Simple](https://www.usbmadesimple.co.uk/) — 初学者向けの解説
- [USB Device Tree Viewer](https://www.uwe-sieber.de/usbtreeview_e.html) — Windowsでの観測
- [TinyUSB](https://docs.tinyusb.org/) — このライブラリが同梱するデバイススタック
- [pid.codes](https://pid.codes/) — オープンソースプロジェクト向けのVID/PID
- このリポジトリの [README.ja.md](../README.ja.md) — API仕様と各クラスの対応状況
- [EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost) — ESP32をホスト側にするライブラリ。[USB Host開発ガイド](https://github.com/tanakamasayuki/EspUsbHost/blob/main/docs/usb-host-guide.ja.md)
