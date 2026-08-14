# USB Device開発ガイド（上級編）

> English: [usb-device-advanced.md](usb-device-advanced.md)

[USB Device開発ガイド](usb-device-guide.ja.md)の続きです。入門編が「ホストに認識させるまで」を扱うのに対し、こちらは**なぜそう動くのか、限界はどこか、限界にぶつかったときに何を測るのか**を扱います。

対象読者は、すでにデバイスを1つ以上動かしたことがあり、次のいずれかに直面している人です。

- ディスクリプタをバイト単位で書く／読む必要がある
- endpoint予算に収まらない、スループットが足りない、転送が止まる
- コールバックのどこで何をしてよいのかを正確に知りたい
- ライブラリが対応していないクラスを自分で足す
- TinyUSBの設定を変えたい、または変えられない理由を知りたい

## 目次

1. [アーキテクチャとタスクモデル](#1-アーキテクチャとタスクモデル)
2. [TinyUSBとの関係](#2-tinyusbとの関係)
3. [ディスクリプタをバイト単位で書く](#3-ディスクリプタをバイト単位で書く)
4. [コントロール転送を受ける側](#4-コントロール転送を受ける側)
5. [エンドポイント資源：採番とFIFO](#5-エンドポイント資源採番とfifo)
6. [転送のタイミングと帯域](#6-転送のタイミングと帯域)
7. [エラーとリカバリ](#7-エラーとリカバリ)
8. [コールバックのコンテキスト](#8-コールバックのコンテキスト)
9. [新しいクラスを実装する](#9-新しいクラスを実装する)
10. [計測とデバッグ](#10-計測とデバッグ)

---

## 1. アーキテクチャとタスクモデル

### 1.1 層構造

```
スケッチ（loop / setup）
  ↕ コールバック登録・送信API
EspUsbDevice + クラス群      … ディスクリプタ生成、クラス状態、バッファ
  ↕ tud_* API / tud_*_cb() の実装
TinyUSB device stack (usbd)  … 標準要求の処理、クラスドライバ、EP0
  ↕ dcd_*
dcd_dwc2                     … endpoint、FIFO、DMA記述子
  ↕ esp_private/usb_phy
USB OTGコントローラ + PHY
```

Host側との一番大きな違いはここです。Host側では下2層（ESP-IDF USB Host LibraryとHCD）がArduino-ESP32のビルド済みバイナリで、**設定を変えられませんでした**。Device側では、**このライブラリがTinyUSBを自分でビルドしています**。したがって下の層も自分のもので、`CFG_TUD_*` は自分で決めた値です（[2章](#2-tinyusbとの関係)）。

Arduino-ESP32から使うのは、SoC定義、PHY初期化（`usb_new_phy()`）、FreeRTOS、ボードサポートまでです。切ったのは Arduino の USB Device 統合であって、コア全体ではありません。

### 1.2 タスクは1本

`begin()` が作るFreeRTOSタスクは**1本だけ**です。Host側が2本（デーモンとクライアント）だったのと対照的です。

| 項目 | 値 |
|------|-----|
| タスク名 | `espusb-device` |
| スタック | 4096バイト |
| 優先度 | `configMAX_PRIORITIES - 1`（**最高優先度**） |
| コア | 固定なし |

ループの中身は2つだけです。

```cpp
while (true) {
  tud_task_ext(1, false);      // 1msの上限つきでUSBイベントを処理
  espUsbDeviceNetDrainTx();    // キューに積まれたNCMフレームを送る
}
```

`tud_task()` ではなく `tud_task_ext(1, false)` を使っているのは、**バスが静かな間もこのタスクを回す必要がある**ためです。`tud_task()` は次のUSBイベントまでブロックするので、その間にキューへ積まれたネットワークフレームがTinyUSBへ渡りません。1msの上限つき待ちにすることで、アイドル時のコストを抑えつつドレインの周期に下限を与えています。

タスクのスタックと優先度は現状 `EspUsbDeviceConfig` から変えられません。優先度が最高なのは、USBのタイミング要求（ホストのポーリングに対する応答）が他の処理に負けないようにするためです。**スケッチ側の重い処理がこのタスクを止めることはありませんが、逆にこのタスクの中で重い処理をすると、システム全体が止まります**（[8章](#8-コールバックのコンテキスト)）。

### 1.3 TinyUSB APIは usbd タスクから呼ぶ

TinyUSBの規約として、device APIは `tud_task()` と同じコンテキストから呼ぶ必要があります。**これはライブラリ内部の実装制約ではなく、守らないと実際に壊れます。**

このライブラリで実測された壊れ方が記録されています。`tud_network_xmit()` を lwIP の tcpip タスクから呼んでいた頃、送信中の完了処理と呼び出しが重なり（測定で**転送完了の約70%が呼び出しの内側に入った**）、IN endpointが「送るパケットはあるがFIFOは空」という状態に落ちました。完了割り込みが二度と来ず、NTBがプールへ戻らず、`tud_network_can_xmit()` が永久に false のまま。**再起動するまでUSBネットワークが死にます。**

そのため現在は、フレームを送る側は**プールされたバッファへコピーしてキューに積むだけ**で、TinyUSBの呼び出しは全部 usbd タスクの `espUsbDeviceNetDrainTx()` が行います。

| 定数 | 値 | 理由 |
|------|-----|------|
| `NET_TX_FRAME_MAX` | 1600 | Ethernetフレーム1本 |
| `NET_TX_SLOTS` | 4（約6.4KB） | usbdタスクの2周回の間にlwIPが渡してくるバーストを吸収する。深くしすぎるとTCPがフィードバックを失う |
| `NET_TX_ACQUIRE_TIMEOUT` | 20ms | 空きが出なければ**フレームを捨てる**。TCPが再送するので、tcpipタスクを止めるより正しい |

この形（producerはコピーしてキューへ、TinyUSBは1タスクから）は、自分でクラスを足すときにもそのまま使える設計です（[9章](#9-新しいクラスを実装する)）。

### 1.4 起動と停止の順序

`begin()` の順序には意味があります。

1. `buildDescriptors()` — ディスクリプタを組み立て、**endpoint予算を検証する**。ここで落ちればPHYは起動しない
2. 各クラスの `begin()` — 失敗したら、それまでに開始したクラスを逆順で `end()`
3. `startTinyUsbRuntime()` — PHY生成 → `tusb_init(rhport)` → タスク生成
4. 各クラスの `afterDeviceStarted()`

つまり、**構成が不正なら電気的に何も起きません**。ホスト側から見ると「挿しても無反応」で、原因はボード内にあります。入門編のBringUpCheckが `BEGIN` の成否を先に表示するのはこのためです。

`config.startTinyUsb = false` にすると、**3以降を行わずディスクリプタだけを組み立てます**。ハードウェアもホストも要らないので、`tests/unit/descriptor` や `tests/unit/composite_constraints` はこれでディスクリプタを検証しています。自分の構成を自動テストしたいときにも使えます。

停止は `stopTinyUsbRuntime()` で、タスク削除 → `tusb_deinit()` → `usb_del_phy()` の逆順です。`end()` の後、同じオブジェクトで `begin()` を再開できます。

---

## 2. TinyUSBとの関係

Device側で最も重要な構造上の事実は、**このライブラリがTinyUSBのソースを同梱して自分でビルドしている**ことです。Arduino-ESP32がprebuildした `libarduino_tinyusb` は使いません。

### 2.1 何を同梱しているか

固定commitのTinyUSBから、**必要な43ファイル（ソース12、ヘッダ31）だけ**を `src/` 以下へ機械的にコピーしています。上流ファイルへのパッチはありません。

| 区分 | ファイル |
|------|---------|
| コア | `tusb.c`, `common/tusb_fifo.c`, `device/usbd.c` |
| クラス | `hid`, `cdc`, `midi`, `msc`, `vendor`, `net/ncm`, `audio` の各 `*_device.c` |
| コントローラ | `portable/synopsys/dwc2/dcd_dwc2.c`, `dwc2_common.c` |

Host、Type-C、DFU、Video、Printer、MTP、MIDI 2.0、ECM/RNDIS、FreeRTOS以外のOSAL、ESP32以外のportableファイルは**コピーしていません**。この一覧はS2/S3/P4のクリーンビルドで生成したコンパイラ依存関係から求めた最小構成です。

由来（リポジトリ、full commit SHA、TinyUSBバージョン、選定理由）は [`third_party/tinyusb/UPSTREAM.json`](../third_party/tinyusb/UPSTREAM.json)、経緯と検証方法は [PROVENANCE.ja.md](../third_party/tinyusb/PROVENANCE.ja.md) にあります。検証スクリプトは固定commitのtarballを取得し、マニフェストの43ファイルだけを展開して**byte-for-byteで比較**します。通常のビルドは何もダウンロードしません。

```sh
python tools/verify_tinyusb_vendor.py
```

`src/tusb_config.h` と `src/internal/EspUsbTinyUsbConfig.h` だけがEspUsbDevice独自の統合ファイルです。

### 2.2 tusb_config.h を所有した結果

`src/tusb_config.h` は `internal/EspUsbTinyUsbConfig.h` を include するだけで、実体はそちらです。主な値:

| 設定 | 値 | 意味 |
|------|-----|------|
| `CFG_TUSB_MCU` | S2 / S3 / P4 で切り替え | 対象外のターゲットは `#error` |
| `CFG_TUSB_OS` | `OPT_OS_FREERTOS` | |
| `CFG_TUD_MAX_SPEED` | P4: `HIGH_SPEED` / それ以外: `FULL_SPEED` | |
| `CFG_TUD_ENDPOINT0_SIZE` | 64 | |
| `CFG_TUD_CDC/MSC/HID/MIDI/AUDIO/VENDOR/NCM` | すべて 1 | **全クラスを常にコンパイルする** |
| `CFG_TUD_CDC_RX/TX_BUFSIZE` | 512 / 512 | |
| `CFG_TUD_MSC_EP_BUFSIZE` | 4096 | SCSIの読み書き単位 |
| `CFG_TUD_HID_EP_BUFSIZE` | 64 | HID endpoint MPSの上限 |
| `CFG_TUD_MIDI_RX/TX_BUFSIZE` | 512 / 512 | |
| `CFG_TUD_VENDOR_RX/TX_BUFSIZE` | 512 / 512 | |
| `CFG_TUD_NCM_IN_NTB_N` | 3 | 上流の既定は1。1だと送信NTBが1本しかなく、毎フレームが前の転送完了を待つ。上流の計測では2で最大50%改善、3で「request blocked」が消える。3200バイト×3で約9.6KB |
| `CFG_TUD_NCM_OUT_NTB_N` | 2 | |

**全クラスを1でコンパイルしている**のが重要な点です。デバイスにどのクラスが現れるかは、Arduino-ESP32のKconfigではなく**ディスクリプタの構成が決めます**。使わないクラスのコードはリンカが落とします。

Audioは「Audio Cardの固定トポロジ」ではなく**コンパイル時の容量**として設定されています。

| 設定 | 値 |
|------|-----|
| `CFG_TUD_AUDIO_MAX_N_CHANNELS` | 2 |
| `CFG_TUD_AUDIO_MAX_N_BYTES_PER_SAMPLE` | 4 |
| 最大サンプルレート | P4: 192000 / それ以外: 96000 |
| ソフトバッファのパケット数 | P4: 8 / それ以外: 4 |

endpointバッファのサイズは、FSとHSの両方の最大パケットから大きい方を取ります。P4のHSコントローラは**Full Speedでネゴシエートすることがある**ため、コンパイル時の確保は両方をカバーし、どのレートが実際に合法かはディスクリプタ検証と実行時判定に任せる、という設計です。

### 2.3 DMAモードとslaveモード

DWC2には2つの転送モードがあり、このライブラリは**DMAモードを使います**。

```c
#define CFG_TUD_DWC2_DMA_ENABLE 1
#define CFG_TUD_DWC2_SLAVE_ENABLE 0
```

slaveモードはCPUが1パケットずつコントローラのTxFIFOへ押し込み、FIFO emptyの割り込みで補充します。その割り込みは最後のバイトを書いた時点で解除されるため、**バルクINを流し続けると「endpointは有効、パケットは残っている、FIFOは空、割り込みは解除済み」という、誰も再開できない状態**に落ちることがあります。CDC-NCMのdevice→host通信が数秒で死んだ実例がこれです。DMAモードはこの経路を通りません。

**2つのモードは排他です。好みの問題ではありません。** `tusb_option.h` は `CFG_TUD_EDPT_DEDICATED_HWFIFO` を `CFG_TUD_DWC2_SLAVE_ENABLE` から導出し、そのフラグが「共有の `tu_edpt_stream` 層（CDC、MIDI、Vendor）がドライバへ実バッファを渡すか `tu_fifo` を渡すか」を決めます。実際にはDMAで動いているのにslaveモードを有効なままにすると、これらのクラスが `usbd_edpt_xfer_fifo()` を呼び、その `xfer->buffer` は NULL なので、**endpointがアドレス0からDMAしてホストにゴミが届きます**。必ずどちらか一方だけを有効にしてください。

P4のキャッシュ整合性は上流が解決しています。3ターゲットのうちL1データキャッシュ越しに内部SRAMへ届くのはP4だけで、`tusb_mcu.h` は**DMAが有効なときだけ** dcache maintenance を有効にします（ラインサイズ64 = `CONFIG_CACHE_L1_CACHE_LINE_SIZE`）。`TUD_EPBUF_TYPE_DEF` が各endpointバッファをキャッシュライン単位でアラインしてパディングするので、DMAバッファが他のデータとラインを共有することはありません。S2/S3にはそのキャッシュがなく、何も要りません。

### 2.4 rhportを実行時に決める

`CFG_TUSB_RHPORT0_MODE` / `CFG_TUSB_RHPORT1_MODE` は**あえて定義していません**。定義すると `TUD_OPT_RHPORT` が固定され、**P4のコントローラ選択がビルド時の決定になってしまう**からです。

代わりに実行時に選びます。

```cpp
// EspUsbTinyUsbRuntime.cpp
g_rhport = highSpeed ? 1 : 0;
const tusb_rhport_init_t init = {
    .role  = TUSB_ROLE_DEVICE,
    .speed = highSpeed ? TUSB_SPEED_HIGH : TUSB_SPEED_FULL,
};
tusb_init(g_rhport, &init);
```

PHYも同じ分岐で作ります。

| controller | `usb_phy_config_t.target` | `otg_speed` | rhport |
|---|---|---|---|
| FullSpeed | `USB_PHY_TARGET_INT`（内蔵PHY） | `USB_PHY_SPEED_FULL` | 0 |
| HighSpeed | `USB_PHY_TARGET_UTMI`（外部UTMI PHY） | `USB_PHY_SPEED_HIGH` | 1 |

P4以外で `HighSpeed` を要求すると、PHYを作る前に `ESP_ERR_NOT_SUPPORTED` で返ります。

### 2.5 TinyUSBが持たないクラスを足す

同梱したTinyUSBは上流そのままなので、**TinyUSBが実装していないクラスをその driver table に足すことはできません**。CCIDがこれに当たります。

TinyUSBには `usbd_app_driver_get_cb()` という weak hook があり、`tusb_init()` が一度だけ読みます。ライブラリはこれを強い定義で上書きし、登録済みのテーブルを返します。

```cpp
// EspUsbDeviceAppDriver.cpp
extern "C" usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *driver_count)
{
  *driver_count = g_appDriverCount;
  return g_appDrivers;
}
```

ここで**間接参照になっているのはフットプリントのため**です。`usbd_app_driver_get_cb` は `usbd.c` から到達可能なので常にリンクされます。もしこの関数がCCIDドライバを直接名指ししていたら、**CCIDを使わないスケッチにもCCIDドライバ全体がリンクされてしまいます**。ポインタを読むだけにしておけば、ドライバへの到達経路は「そのクラスを実体化したスケッチ」だけになり、使わなければリンカが落とします。

登録は各クラスの `begin()` から、**スタック起動前に**行います。

```cpp
espUsbDeviceRegisterAppDrivers(drivers, count);  // drivers はスタック稼働中ずっと有効であること
```

---

## 3. ディスクリプタをバイト単位で書く

[`EspUsbDeviceDescriptorDump`](../examples/Info/EspUsbDeviceDescriptorDump/) が出す生バイトを読むための表です。USBの多バイト値は**すべてリトルエンディアン**です。Host側の入門編と違い、ここでの関心は「**どのフィールドを自分が決めていて、どれをライブラリが決めているか**」です。

### 3.1 デバイスディスクリプタ（18バイト）

| オフセット | サイズ | フィールド | 誰が決めるか |
|-----------|--------|-----------|------------|
| 0 | 1 | bLength | 18 固定 |
| 1 | 1 | bDescriptorType | 0x01 |
| 2 | 2 | bcdUSB | ライブラリ（WebUSB有効時は0x0201、通常0x0200） |
| 4 | 1 | bDeviceClass | **常に0x00**（インターフェース側で決まる） |
| 5 | 1 | bDeviceSubClass | 常に0x00 |
| 6 | 1 | bDeviceProtocol | 常に0x00 |
| 7 | 1 | bMaxPacketSize0 | 64（`CFG_TUD_ENDPOINT0_SIZE`） |
| 8 | 2 | idVendor | **`config.vid`** |
| 10 | 2 | idProduct | **`config.pid`** |
| 12 | 2 | bcdDevice | ライブラリ |
| 14–16 | 各1 | iManufacturer / iProduct / iSerialNumber | **`config.manufacturer` / `product` / `serialNumber`** の有無 |
| 17 | 1 | bNumConfigurations | 1 |

押さえておく点が2つあります。

**`bcdUSB` が上がるのはWebUSB有効時だけです。** BOSディスクリプタを持つと宣言するにはUSB 2.01以上が必要で、ここを変えずにBOSだけ足してもホストは取りに来ません。このライブラリが返すのは 0x0201 です（WebUSB仕様自体は 0x0210 を求めていますが、ホストはBOS取得の可否を「2.01以上か」で判断するため、実際には取りに来ます）。

**bDeviceClass は複合デバイスでも 0x00 のままです。** 「何であるか」は完全にインターフェース側にあり、複数機能のまとめ方はコンフィグレーションディスクリプタ内のIAD（型 0x0b）が担います。CDCは `TUD_CDC_DESCRIPTOR` の一部としてIADを出すので、CDCを含む複合デバイスにはIADが入ります。IADを使うデバイスはデバイスレベルでも 0xef/0x02/0x01 を宣言するのが規格上の作法ですが、このライブラリはそうしていません。ホスト側のドライバのバインドがおかしいときは、ここを疑う価値があります（[入門編5.5](usb-device-guide.ja.md#55-うまくいっている機器と比べる)の差分取りが効く典型例です）。

### 3.2 コンフィグレーションディスクリプタ（9バイト＋後続）

| オフセット | フィールド | 誰が決めるか |
|-----------|-----------|------------|
| 2–3 | wTotalLength | ライブラリ（上限704バイト） |
| 4 | bNumInterfaces | 登録クラスから算出 |
| 5 | bConfigurationValue | 1 |
| 7 | bmAttributes | bit7=1固定、bit6=**`config.selfPowered`** |
| 8 | bMaxPower | **`config.maxPowerMilliamps` ÷ 2** |

この9バイトの後ろに、インターフェース／エンドポイント／クラス固有ディスクリプタが連結します。走査は「先頭バイトが長さ、2バイト目が型」の繰り返しだけです。

### 3.3 インターフェースとエンドポイント

インターフェース番号とエンドポイント番号は、**登録順に一括採番されます**（[5.1](#51-採番規則)）。手で決めるものではありません。

エンドポイントディスクリプタ（7バイト）で自分の設計が出るのは3つです。

| オフセット | フィールド | 備考 |
|-----------|-----------|------|
| 2 | bEndpointAddress | bit7=方向、bit3:0=番号。ライブラリが採番 |
| 3 | bmAttributes | 転送タイプ。クラスが決める |
| 4 | wMaxPacketSize | 速度とクラスで決まる（[6.2](#62-最大パケットサイズ)） |
| 6 | bInterval | ポーリング間隔の**要求**。保証ではない（[6.1](#61-bintervalは要求であって保証ではない)） |

### 3.4 HIDディスクリプタとレポートディスクリプタ

コンフィグレーションディスクリプタの中のHIDディスクリプタ（型 0x21、9バイト）は、**レポートディスクリプタの長さしか持ちません**。中身はホストが `GET_DESCRIPTOR(type=0x22)` で別途取りに来ます。

| オフセット | フィールド |
|-----------|-----------|
| 2–3 | bcdHID |
| 5 | bNumDescriptors |
| 6 | bDescriptorType（0x22） |
| 7–8 | **wDescriptorLength**（レポートディスクリプタのバイト数） |

DescriptorDumpがレポートディスクリプタの長さをここから読んでいるのはこのためです。

### 3.5 複合HIDのレポートID結合

複合HID（キーボード＋マウスなど）では、各クラスのレポートディスクリプタが**1本に連結されます**。連結の規則は次のとおりです。

```
[クラスAの先頭6バイト] [0x85 レポートID_A] [クラスAの残り]
[クラスBの先頭6バイト] [0x85 レポートID_B] [クラスBの残り]
...
```

先頭6バイトは Usage Page / Usage / Collection にあたり、その直後に `0x85`（Report ID）アイテムを挿入します。レポートIDは固定です。

| クラス | レポートID |
|--------|-----------|
| Keyboard | 1 |
| Mouse | 2 |
| Gamepad | 3 |
| Consumer Control | 4 |
| System Control | 5 |
| Vendor | 6 |

結果として、複合HIDは**インターフェースが増えるのではなく、1つのHIDインターフェース上のレポートIDが増えます**。エンドポイントはEP1の1本（OUT=0x01 / IN=0x81）を共有する双方向構成です。

上限は `MAX_HID_REPORT_DESCRIPTOR` = 256バイトで、超えると `begin()` が失敗します。

### 3.6 文字列ディスクリプタ

`index=0` は言語IDリストで、このライブラリは 0x0409（en-US）を1つだけ返します。`index=1,2,3` が manufacturer / product / serialNumber、`index=4` はネットワーク機能があるときのMACアドレス文字列です。

実体はUTF-16LEですが、**ライブラリはASCIIを1文字ずつ16bitへ拡張するだけ**です。上限63文字で、超えると切り詰められます。非ASCII文字を製品名に入れても、そのままでは正しく出ません。

### 3.7 BOSとMicrosoft OS 2.0

`config.webusbEnabled = true` のときだけ生成されます。

| ディスクリプタ | サイズ | 内容 |
|---------------|--------|------|
| BOS | 最大57バイト | WebUSB platform capability（landing URL）と Microsoft OS 2.0 platform capability |
| MS OS 2.0 | 178バイト | 実際に割り当てたvendorインターフェースに対する WinUSB compatible ID と device interface GUID |

**Windowsでvendorインターフェースを開けるようにするのがMS OS 2.0の役割**です。これがないと、`0xff` のインターフェースはドライバなしのまま残ります。vendor code、GUID、内容を差し替えるAPIは未実装です。

---

## 4. コントロール転送を受ける側

### 4.1 setupパケットの8バイト

Host側では「送る側」として読んだ表を、こちらは「受ける側」として読みます。

| バイト | フィールド | 内容 |
|--------|-----------|------|
| 0 | bmRequestType | bit7=方向(1=IN) / bit6:5=型(0:標準 1:クラス 2:ベンダー) / bit4:0=宛先(0:デバイス 1:インターフェース 2:エンドポイント) |
| 1 | bRequest | 要求番号 |
| 2–3 | wValue | 要求ごとの意味 |
| 4–5 | wIndex | インターフェース番号やエンドポイントアドレス |
| 6–7 | wLength | データステージのバイト数 |

### 4.2 どの要求がどのコールバックに来るか

標準要求（`SET_ADDRESS`、`GET_DESCRIPTOR`、`SET_CONFIGURATION` など）はTinyUSBの `usbd.c` が処理し、必要な内容だけをライブラリのコールバックへ聞きに来ます。

| ホストの要求 | 呼ばれるもの |
|-------------|-------------|
| `GET_DESCRIPTOR(DEVICE)` | `tud_descriptor_device_cb()` |
| `GET_DESCRIPTOR(CONFIGURATION)` | `tud_descriptor_configuration_cb(index)` |
| `GET_DESCRIPTOR(STRING)` | `tud_descriptor_string_cb(index, langid)` |
| `GET_DESCRIPTOR(BOS)` | `tud_descriptor_bos_cb()` |
| `GET_DESCRIPTOR(DEVICE_QUALIFIER)` | `tud_descriptor_device_qualifier_cb()` |
| `GET_DESCRIPTOR(OTHER_SPEED_CONFIG)` | `tud_descriptor_other_speed_configuration_cb(index)` |
| `GET_DESCRIPTOR(HID REPORT)` | `tud_hid_descriptor_report_cb(instance)` |
| `SET_CONFIGURATION n` | `tud_mount_cb()` → 各クラスの `onBusAttached()` |
| `SET_CONFIGURATION 0` / 切断 | `tud_umount_cb()` → 各クラスの `onBusDetached()` |
| HID `SET_REPORT` | `tud_hid_set_report_cb()` → クラスの `onHidSetReport()` |
| HID `GET_REPORT` | `tud_hid_get_report_cb()` |
| HID `SET_PROTOCOL` | `tud_hid_set_protocol_cb()` → `onHidSetProtocol()` |
| CDC `SET_LINE_CODING` | `tud_cdc_line_coding_cb()` |
| CDC `SET_CONTROL_LINE_STATE` | `tud_cdc_line_state_cb()` |
| vendor / WebUSB のクラス・ベンダー要求 | `tud_vendor_control_xfer_cb()` → `onControlRequest()` |
| MSC の各SCSIコマンド | `tud_msc_*_cb()` 群 |

つまり、**EP0の処理は書かなくてよいが、答える内容は全部こちらが持っている**という構造です。

### 4.3 3つのステージとSTALL

コントロール転送は Setup →（Data）→ Status の3ステージです。要求を扱えないときは**STALL**を返します。

**STALLは故障ではなく「その要求はサポートしていない」という正当な回答です。** ホスト側は標準要求以外については、STALLを想定して作られています。`onControlRequest()` で `false` を返すとSTALLになります。

`EspUsbDeviceVendor::onControlRequest()` は `stage` 付きで呼ばれるので、ステージごとの処理が書けます。IN方向なら `sendControlResponse(request, data, length)` でデータステージを返し、OUT方向なら `sendControlResponse(request)` でstatusステージだけを成功させます。

```cpp
vendor.onControlRequest([](const EspUsbDeviceVendorControlRequest &r) {
  if ((r.bmRequestType & 0x80) && r.bRequest == 0x01) {
    return vendor.sendControlResponse(r, info, min<size_t>(r.wLength, sizeof(info) - 1));
  }
  return false;   // → STALL
});
```

`wLength` を超えて返そうとしないこと、そして**要求されたバイト数より少なく返すのは正当**（short packetでデータステージが終わる）であることを押さえておいてください。

---

## 5. エンドポイント資源：採番とFIFO

### 5.1 採番規則

`buildDescriptors()` が、インターフェース番号0・エンドポイント番号1から**登録順に**割り当てます。

1. **HIDが先**。複合HIDなら1インターフェース＋EP1の双方向1本（`0x01` / `0x81`）。単独HIDならクラスごとに1インターフェース＋IN 1本
2. 続いて非HIDクラスを登録順に。各クラスの `interfaceCount()` と `endpointCount()` の分だけ番号が進む

クラスごとの実際のアドレスの取り方は、たとえばCDCなら次のとおりです。

```cpp
epNotification = 0x80 | n;      // 通知用 IN
epOut          = n + 1;         // データ OUT
epIn           = 0x80 | (n + 1) // データ IN
```

つまり**CDCはエンドポイント番号を2つ消費します**（通知とデータで別番号）。NCMも同じ形です。Vendorは1番号をIN/OUT両方に使います（`n` と `0x80 | n`）。

**実際に割り当てられた番号は必ずDescriptorDumpで確認してください。** クラスの登録順を変えると番号が変わり、ホスト側のスクリプトがエンドポイントアドレス直書きなら壊れます。

### 5.2 controller上限の実際

`validateControllerEndpoints()` が、組み立てた**ディスクリプタを走査して**チェックします。クラスの申告ではなく実際に書き出されたエンドポイントを数えるので、見落としはありません。

| controller | endpoint番号 | control以外のIN | control以外のOUT |
|---|---|---|---|
| ESP32-S2 / S3 | 5 | 4 | 5 |
| ESP32-P4 rhport 0（FS） | 6 | 4 | 6 |
| ESP32-P4 rhport 1（HS） | 15 | 7 | 15 |

ソース中の注記によれば、P4のrhport 0はエンドポイント番号が全7・IN 5本（EP0含む）、rhport 1は全16・IN 8本（EP0含む）です。上の表はそこからEP0を引いた値です。

超えると `begin()` は `ESP_ERR_INVALID_SIZE` を返します。**PHYを起動する前に落ちる**ので、ホスト側には何も起きません。

### 5.3 なぜIN方向が先に枯れるのか

DWC2では、**IN endpointごとに専用のTxFIFOが必要**です。OUTは共有のRxFIFOを使います。したがってIN方向の本数がハードウェアの資源に直接縛られ、実際にS2/S3では control以外のINが4本で頭打ちになります。

これが「HID + CDC + MSC でちょうど上限」の理由です。IN の内訳は HID 1 + CDC 2（通知＋データ）+ MSC 1 = 4。ここに何を足しても入りません。

回避の順序は次のようになります。

1. **クラスを減らす。** CDCは通知用INを1本使うので、単にバイト列を流したいだけならVendor（IN 1本）の方が安い
2. **複合HIDにまとめる。** キーボード＋マウス＋ゲームパッドはIN 1本で済む
3. **ESP32-P4のHSコントローラを使う。** IN 7本まで増える

### 5.4 バッファのサイズ

Host側のFIFO分割に相当する調整はDevice側にはありません（`dcd_dwc2` が確保します）。代わりに効いてくるのは `CFG_TUD_*_BUFSIZE` です（[2.2](#22-tusb_configh-を所有した結果)）。

| 症状 | 見るところ |
|------|----------|
| CDCで取りこぼす | `CFG_TUD_CDC_RX_BUFSIZE`（512） |
| MSCが遅い | `CFG_TUD_MSC_EP_BUFSIZE`（4096）。SCSIの読み書き1回の単位 |
| NCMのスループットが出ない | `CFG_TUD_NCM_IN_NTB_N`（3）と `NET_TX_SLOTS`（4） |
| HIDのレポートが大きくて入らない | `CFG_TUD_HID_EP_BUFSIZE`（64）がHID endpoint MPSの上限 |

これらは同梱の設定ファイルの値なので、**ライブラリを変更すれば変えられます**。Host側と違って「Arduinoのビルド済みだから無理」ではありません。ただし変えるとRAM消費が増え、上流と差分が出ます。

---

## 6. 転送のタイミングと帯域

### 6.1 bIntervalは要求であって保証ではない

デバイスは「この間隔でポーリングしてほしい」と申告できますが、**実際の間隔を決めるのはホスト**です。

| 速度・種別 | bIntervalの解釈 |
|-----------|----------------|
| FS interrupt | そのままミリ秒（1〜255） |
| FS iso | `2^(bInterval-1)` フレーム |
| HS interrupt / iso | `2^(bInterval-1)` マイクロフレーム（bInterval=4 → 8×125µs = 1ms） |

「HIDの反応が遅い」と感じたときは、まず自分のbIntervalを確認し、次に**ホスト側の実測**を見てください。Linuxなら `evtest` のタイムスタンプ、あるいは `usbmon` のキャプチャで実際の間隔が出ます。デバイス側でいくら小さく申告しても、ホストのスケジューリングとバスの混雑には勝てません。

### 6.2 最大パケットサイズ

| 転送 | FS | HS |
|------|----|----|
| Control | 8/16/32/64（このライブラリは64） | 64 |
| Bulk | 8/16/32/64（このライブラリは64） | **512固定** |
| Interrupt | ≤64 | ≤1024 |
| Isochronous | ≤1023 | ≤1024 |

このライブラリのHS用コンフィグレーションディスクリプタは、FS版をコピーしてから**bulkエンドポイントのMPSだけを512へ書き換えて**作られます。Audioは方向とレートから別途計算されます。

HIDのMPSは構成で変わります。

| 構成 | HID endpoint MPS |
|------|-----------------|
| 単独HID | 8 |
| 複合HID | 16 |
| 複合HIDにNKROキーボードを含む | そのクラスが要求する大きさ（`CFG_TUD_HID_EP_BUFSIZE` = 64 が上限） |

NKROキーボードはビットマップレポートが1パケットに収まらないと分割されるため、`hidInEndpointSize()` でより大きなMPSを要求します。複合HIDの共有エンドポイントは、**含まれるクラスの要求の最大値**を取ります。

### 6.3 実測スループット

| | 条件 | 実測 |
|---|---|---|
| ESP32-P4 HS bulk | `tests/manual/p4_hs_bulk`、512バイト同期echo | スクリプトが MiB/s を表示 |

`p4_hs_bulk` が表示する値は**パケットごとの同期echoを含む健全性確認値であり、最大帯域のベンチマークではありません**。1パケット送って1パケット受けるたびにバスが空くので、実運用のストリーミングより低く出ます。設計の見積りに使うなら、自分の転送パターンで測り直してください。

なお、512バイトちょうどの転送を `flush()` するとTinyUSBは終端のZLPを送ります。チェッカがこの正規の0バイトパケットを数えて読み飛ばしているのは、それが**プロトコル上正しい**からです（[7.2](#72-zlp)）。

---

## 7. エラーとリカバリ

### 7.1 NAKとSTALLを返す側

- **NAK** … 「今は用意がない」。ホストが再試行するのでエラーではありません。デバイス側では、送るデータがないIN endpointが自動的にNAKを返します
- **STALL** … 「その要求／転送は扱えない」。コントロール転送では正当な回答です（[4.3](#43-3つのステージとstall)）。バルク／インタラプトのendpointでSTALLすると halt 状態になり、ホストが `CLEAR_FEATURE(ENDPOINT_HALT)` を送るまで通りません

### 7.2 ZLP

バルク転送は**MPS未満のパケット（short packet）**で終わります。転送長がMPSの倍数ちょうどのとき、受け手は「まだ続く」と解釈するので、プロトコルによっては**ZLP（長さ0パケット）**が終端として必要です。

TinyUSBは、ちょうどMPSの倍数の転送を `flush()` したときにZLPを送ります。ホスト側のスクリプトを書くときは、**この0バイトパケットが来ることを前提にしてください**。「特定サイズでだけ止まる」「たまに空の読み出しが返る」はここが原因です。

### 7.3 バスリセット、サスペンド、デコンフィグレーション

デバイス側の状態は、次の3つで無効になります。

| 出来事 | 起きること |
|--------|-----------|
| `SET_CONFIGURATION 0` | `tud_umount_cb()` → `onBusDetached()` |
| 切断（VBUS検出があるボードのみ） | 同上 |
| `SET_CONFIGURATION n` | `tud_mount_cb()` → `onBusAttached()` |

**フックが2つある理由**が重要です。素のバスリセットは `onBusDetached()` に届きません。VBUSセンシングを配線していないボード（大半のESP32ボード）では、抜線も届きません。したがって**挿し直しは検出されないまま再列挙へ進みます**。常に発火するのは `onBusAttached()`（`SET_CONFIGURATION n`）の方で、これが穴を塞いでいます。mount時点ではホストは「何も押されていない」「LEDは設定していない」と認識しているので、ここで状態を捨てるのはどちらにせよ正しい動作です。

自分でクラスを書くときも、**ホストが知っていると信じている状態は両方のフックで捨ててください**。

### 7.4 未mount時の送信

`device.ready()`（= `tud_mounted()`）が false のときの送信は、キューに積まれず**捨てられます**。送信APIは `false` を返します。

これが「送っているのに届かない」の最も多い原因です。入門編のConsoleが送信前に `ready()` を確認して明示的なエラーを出すのは、`false` という戻り値だけでは理由がわからないからです。

---

## 8. コールバックのコンテキスト

### 8.1 全部 usbd タスクで走る

**ライブラリが呼ぶコールバックは、すべて `espusb-device` タスク（usbdタスク）で走ります。** `loop()` とは別タスクで、しかも**優先度は最高**です。

| コールバック | 契機 |
|-------------|------|
| `keyboard.onOutputReport()` | ホストのLEDレポート |
| `keyboard.onProtocol()` | boot / report protocol の切り替え |
| `cdc.onRx()` / `onLineCoding()` / `onLineState()` | CDCの受信・制御 |
| `vendor.onRx()` / `onControlRequest()` | vendor bulk OUT・コントロール要求 |
| `hidVendor.onOutputReport()` / `onFeatureReport()` | HIDのOUT/FEATUREレポート |
| MSCのSCSIコールバック | ホストの読み書き |
| Audioのイベント、Netのフレーム受信 | 各クラス |

制約は明確です。

| してよいこと | してはいけないこと |
|-------------|------------------|
| データをコピーする、フラグを立てる、キューに積む | `delay()`、長いループ、完了待ち |
| 短いログ出力 | 大きなヒープ確保、ファイルI/O、ネットワーク処理 |
| 状態変数の更新 | 別タスクのロック待ち |

**このタスクが最高優先度で動いていることを忘れないでください。** ここで100ms待つと、その間USBの応答が全部止まるだけでなく、優先度の低いタスクは一切走りません。

正しい形は Host側と同じです。

```cpp
volatile bool workRequested = false;

vendor.onRx([](size_t available) {
  vendor.read(rxBuffer, min(available, sizeof(rxBuffer)));  // コピーだけ
  workRequested = true;                                      // 処理は loop() で
});

void loop() {
  if (workRequested) {
    workRequested = false;
    doSomethingExpensive();
  }
}
```

**データポインタの寿命**も同じです。`onOutputReport()` などが渡す `data` はライブラリ内部のバッファで、戻ると再利用されます。後で使うならコピーしてください。

### 8.2 なぜ高頻度I/Oがpolling APIなのか

Audio と Network の本体データは、**コールバックではなく polling API** になっています。

```cpp
// Audio: コールバックではなく、自分の周期で読む
int n = playback.available();
size_t got = playback.read(buffer, sizeof(buffer));

// Network: 生フレームは sendFrame() / onFrame()
```

理由は8.1の裏返しです。48kHz/2ch/16bitのオーディオは1msあたり192バイト来ます。これをコールバックで渡すと、**アプリケーションのDSPやI2S書き込みがusbdタスクの中で走る**ことになり、USBのタイミングと競合します。境界をFIFOで切り、アプリは自分のタスクの都合で読み書きする、という設計です。

その代わり、**読み書きが遅れればオーバーラン／アンダーランになります**。これは隠さずカウントされます。

```cpp
EspUsbAudioStreamStats s = playback.stats();
// s.transferredBytes / s.overrunCount / s.overrunBytes
//                     / s.underrunCount / s.underrunBytes
```

`overrunCount` が増えるなら読み出しが遅い、`underrunCount` が増えるなら書き込みが間に合っていません。**音が途切れるときは、まずこの数字を見てください。** 「なんとなく途切れる」を「1秒あたり何回、何バイト落ちている」に変えられます。

Networkの送信側も同じ思想で、キューが埋まればフレームを捨ててTCPの再送に任せます（[1.3](#13-tinyusb-apiは-usbd-タスクから呼ぶ)）。

---

## 9. 新しいクラスを実装する

### 9.1 EspUsbDeviceClass を継承する

ライブラリのクラスはすべて `EspUsbDeviceClass` の派生です。コンストラクタが `device.addClass(this)` を呼ぶので、**オブジェクトを作るだけで登録されます**（4個まで）。

実装すべきものは次のとおりです。

| メンバ | 役割 |
|--------|------|
| `configurationDescriptor(dst, interfaceNumber, endpointNumber, endpointSize)` | **必須。** 割り当てられた番号を使って自分のディスクリプタを書き、書いたバイト数を返す |
| `interfaceCount()` / `endpointCount()` | **必須。** 採番を進める量 |
| `isHid()` / `isCdc()` / `isMsc()` / `isVendor()` / `isAudio()` / `isNet()` / `isMidi()` | 種別。既定は `isHid()` が true |
| `begin()` / `end()` | スタック起動前の準備と後始末 |
| `afterDeviceStarted()` | スタック起動後の処理 |
| `configurationDescriptorForSpeed(dst, capacity, ..., highSpeed)` | 速度で内容が変わる場合。既定はMPSを64/512で切り替えて `configurationDescriptor()` を呼ぶ |
| `hidReportDescriptor()` / `hidReportDescriptorLength()` / `hidReportId()` / `hidInEndpointSize()` | HIDの場合 |
| `onBusAttached()` / `onBusDetached()` | ホスト側の認識が無効になったときに状態を捨てる（[7.3](#73-バスリセットサスペンドデコンフィグレーション)） |

**`configurationDescriptor()` は渡された番号をそのまま使うこと**が重要です。自分で番号を決めると採番が壊れ、`validateControllerEndpoints()` の検査もすり抜けます。

### 9.2 TinyUSBが持っているクラスなら

CDC、HID、MIDI、MSC、Vendor、NCM、Audioは**すでにコンパイルされています**（[2.2](#22-tusb_configh-を所有した結果)）。したがって新しい機能は、多くの場合「既存クラスの上にプロトコルを載せる」だけで済みます。

- 独自データをドライバなしで流したい → `EspUsbDeviceHidVendor`（63バイトレポート）または `EspUsbDeviceHidCustom`（自前のレポートディスクリプタ）
- 帯域が要る独自プロトコル → `EspUsbDeviceVendor`（bulk IN/OUT ＋ control）
- シリアルに見せる → `EspUsbDeviceCdcSerial`

### 9.3 TinyUSBが持っていないクラスなら

CCIDが実例です。手順は次のようになります。

1. `usbd_class_driver_t` を実装する（`init` / `reset` / `open` / `control_xfer_cb` / `xfer_cb` / `sof`）
2. クラスの `begin()` から `espUsbDeviceRegisterAppDrivers(drivers, count)` を呼ぶ。**スタック起動前**であること
3. `configurationDescriptor()` でクラス固有ディスクリプタを含めて書き出す
4. ドライバのテーブルは**スタックが動いている間ずっと有効**でなければならない（staticにする）

`usbd_app_driver_get_cb` を直接名指ししないこと（[2.5](#25-tinyusbが持たないクラスを足す)）。フットプリントのための間接参照を壊すと、そのクラスを使わないスケッチにもドライバがリンクされます。

### 9.4 実装の順序

1. **`config.startTinyUsb = false` でディスクリプタだけ作る。** ハードウェア不要。`tests/unit/descriptor` と同じやり方で、バイト列が意図どおりか先に固める
2. **DescriptorDumpに載せて、endpoint予算を確認する**
3. **実機で列挙させ、`device_inspect` でホスト側と突き合わせる**
4. **データ経路を1方向ずつ確認する**（まずデバイス→ホスト、次にホスト→デバイス）
5. **`enumeration_soak` を回す。** 再列挙で状態が壊れないか
6. **エラー経路を試す**（未mountでの送信、抜線、ホストのサスペンド）

### 9.5 参考にする実装

| やりたいこと | 参考 |
|-------------|------|
| 最小のクラス実装 | `EspUsbDeviceHidMouse`（1インターフェース、IN 1本） |
| 複数インターフェース＋クラス固有ディスクリプタ | `EspUsbDeviceCdcSerial`（IAD＋機能ディスクリプタ） |
| bulk双方向＋control | `EspUsbDeviceVendor` |
| TinyUSB外のクラスドライバ | `EspUsbDeviceCcid` と `internal/EspUsbDeviceAppDriver.*` |
| 速度で内容が変わるディスクリプタ | `EspUsbAudioFunction` |
| usbdタスクとの受け渡し | `EspUsbDeviceNet` のTXキュー |

---

## 10. 計測とデバッグ

### 10.1 何を測るか

| 知りたいこと | 手段 |
|-------------|------|
| 自分が宣言している構成 | [`EspUsbDeviceDescriptorDump`](../examples/Info/EspUsbDeviceDescriptorDump/) |
| endpoint予算に収まるか | 同上（ホスト接続不要） |
| ホストが受け取った内容 | [`device_inspect`](../tests/manual/device_inspect/) と `lsusb -v` |
| 任意の転送を試す | [`EspUsbDeviceConsole`](../examples/Info/EspUsbDeviceConsole/) |
| 再列挙耐性 | [`enumeration_soak`](../tests/manual/enumeration_soak/) |
| HSの実効速度 | [`p4_hs_bulk`](../tests/manual/p4_hs_bulk/) |
| Audioの取りこぼし | `stats()` の overrun / underrun |
| ディスクリプタの自動検証 | `config.startTinyUsb = false` ＋ [`tests/unit/`](../tests/unit/) |
| 2台構成での自動テスト | [`tests/peer/`](../tests/peer/) |
| P4のポート特定 | [`tests/probe/`](../tests/probe/) |

`tests/probe/` は正式な回帰テストではなく、**ブリングアップと切り分けのための使い捨てスケッチ置き場**です。同じ用途の新しい調査を始めるときは、ここに追加するのが既存の作法です。

### 10.2 ログを読む

Core Debug Level を `Verbose` にすると、ESP-IDF側（PHY、コントローラ）のログが出ます。TinyUSB自身のログは既定で切ってあります（`CFG_TUSB_DEBUG 0`）。

TinyUSBのログを出したい場合は `CFG_TUSB_DEBUG` を 1〜3 に上げてビルドし直します。**ただし出力量が多く、USBのタイミングそのものに影響します。** 列挙が通らない原因の切り分けでは、まずホスト側のログ（`dmesg -w` / USB Device Tree Viewer）の方が情報量が多いことがほとんどです。

### 10.3 切り分けの原則

1. **`begin()` が成功したかを先に見る。** 失敗しているなら、ホストは一切関係ありません
2. **ホストにつながずにDescriptorDumpを読む。** ディスクリプタが意図どおりでなければ、その先を調べても無駄です
3. **自分の出力とホストの受け取りを突き合わせる。** 一致していれば、問題はディスクリプタより下か上のどちらかに絞れます
4. **クラスを1つずつ外す。** 複合デバイスの問題は、単機能に戻すと消えることが多く、消えたなら原因は予算か並び順です
5. **別のホストOSで試す。** Windowsだけ、macOSだけで起きる問題は珍しくありません。OSごとの癖は[入門編5章](usb-device-guide.ja.md#5-ホストosから自分を観測する)にまとめています
6. **同時に複数変えない。** クラス構成、MPS、バッファサイズは1つずつ

---

## 関連ドキュメント

- [USB Device開発ガイド（入門編）](usb-device-guide.ja.md) — 基礎、コネクタ、実験手順、ホストOSからの観測
- [README.ja.md](../README.ja.md) — APIリファレンスとクラス対応状況
- [third_party/tinyusb/PROVENANCE.ja.md](../third_party/tinyusb/PROVENANCE.ja.md) — 同梱TinyUSBの由来と検証方法
- [docs/DESIGN_NOTES.ja.md](DESIGN_NOTES.ja.md) — 設計の背景
- [docs/V2_ARCHITECTURE.ja.md](V2_ARCHITECTURE.ja.md) — v2のアーキテクチャ
- [tests/manual/README.ja.md](../tests/manual/README.ja.md) — マニュアルテスト一覧
- [tests/TEST_PLAN.ja.md](../tests/TEST_PLAN.ja.md) — テスト戦略
- [EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost) — ホスト側。[USB Host開発ガイド（上級編）](https://github.com/tanakamasayuki/EspUsbHost/blob/main/docs/usb-host-advanced.ja.md)
