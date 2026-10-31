# USB Device開発ガイド（上級編）

> English: [usb-device-advanced.md](usb-device-advanced.md)

[USB Device開発ガイド](usb-device-guide.ja.md)の続きです。
入門編は「Hostに認識させるまで」を扱います。この文書は、全classに共通する仕組みを扱います。
なぜそう動くのか、限界はどこか、限界に当たったら何を見ればよいかを書きます。

次のような人を想定しています。

- descriptorをbyte単位で読みたい、確かめたい
- endpointやFIFOの予算に収まらない、転送が止まる、思ったより遅い
- callbackの中で何をしてよいかを正確に知りたい
- copyなしの借用転送（`readDirect`/`writeDirect`）を使いたい
- PSRAM、cache、HSとFSの違いを理解したい

各classの詳しい使い方は、class別の文書（[HID](classes/hid.ja.md)、[CDC](classes/cdc.ja.md)、
[Vendor](classes/vendor.ja.md)、[MIDI](classes/midi.ja.md)、[MSC](classes/msc.ja.md)、[Audio](classes/audio.ja.md)、
[Video](classes/video.ja.md)、[NCM](classes/ncm.ja.md)、[CCID](classes/ccid.ja.md)、[DFU](classes/dfu.ja.md)）に任せます。

この文書はv3を対象にします。v2との違いは[14章](#14-v2との違い)にまとめました。

## 目次

1. [層の構成と依存の範囲](#1-層の構成と依存の範囲)
2. [taskモデルとcallbackの文脈](#2-taskモデルとcallbackの文脈)
3. [開始と停止](#3-開始と停止)
4. [descriptorの生成](#4-descriptorの生成)
5. [control転送](#5-control転送)
6. [endpoint番号とFIFO](#6-endpoint番号とfifo)
7. [HSとFS](#7-hsとfs)
8. [cacheとDMA](#8-cacheとdma)
9. [copy転送と借用転送](#9-copy転送と借用転送)
10. [PSRAMの使い方](#10-psramの使い方)
11. [suspendとremote wakeup](#11-suspendとremote-wakeup)
12. [独自のFunctionを足す](#12-独自のfunctionを足す)
13. [計測とデバッグ](#13-計測とデバッグ)
14. [v2との違い](#14-v2との違い)
15. [確認した範囲](#15-確認した範囲)

用語：

| 用語 | 意味 |
| --- | --- |
| Device | `EspUsbDevice`。USB device全体。同時に動かせるのは1つだけ |
| Function | `EspUsbDeviceCdcSerial`などのclass object。Deviceに登録して使う |
| MPS | wMaxPacketSize。1 packetの最大長 |
| FS / HS | Full Speed（12 Mbps）/ High Speed（480 Mbps） |
| ZLP | Zero Length Packet。長さ0のpacket |
| USB worker | ライブラリが作るFreeRTOS task。USBのeventとclass処理を受け持つ |
| 借用転送 | アプリのbufferをcopyせずにDMAへ渡す転送 |

---

## 1. 層の構成と依存の範囲

### 1.1 層

```text
sketch（setup / loop / 自分のtask）
  ↓ configure()、begin()、送受信API、callbackの登録
EspUsbDevice と Function群         … 構成、descriptor生成、classごとのring・状態
  ↓ class driver表（選んだclassだけ）
device core（EspUsbDeviceCore）    … EP0、標準要求、event queue、世代の管理
  ↓ controller境界（EspUsbController.h）
DWC2 driver（EspUsbDwc2*）          … register、FIFO、DMA、割込み
  ↓ ESP-IDF：usb_phy、esp_intr、esp_cache、heap_caps、FreeRTOS
USB OTG controller と PHY
```

| 層 | 主なfile | 役割 |
| --- | --- | --- |
| Device / Function | `src/EspUsbDevice.h`、`src/*.cpp` | 公開API。構成の検証、descriptorの生成、classの処理 |
| runtime | `src/internal/EspUsbRuntime.*` | PHYの取得、controllerの選択、USB workerの起動と停止 |
| device core | `src/internal/EspUsbDeviceCore.*` | EP0、標準要求、event queue、busと構成の世代 |
| controller境界 | `src/internal/EspUsbController.h` | coreとDWC2の間の型付きの入口 |
| DWC2 | `src/internal/EspUsbDwc2Device.c`、`EspUsbDwc2Phy.c`、`EspUsbDwc2Registers.h`、`EspUsbDwc2Hardware.h` | endpoint、FIFO、DMA、割込み |
| platform | `src/internal/EspUsbDwc2Platform.*` | SoCごとのport表、IRQの登録、cache同期 |
| wire型 | `src/internal/EspUsbWire.h` | SETUP（8 byte）、interface（9 byte）、endpoint（7 byte）のlayout |

`src/internal/`は利用者APIではありません。名前や中身は予告なく変わります。

### 1.2 TinyUSBとの関係

v3は、TinyUSBのdevice stack（`usbd.c`）もclass driverも使いません。
EP0、標準要求、class処理は、このライブラリのcoreとFunctionが行います。

DWC2 driverだけは、TinyUSBのDWC2 driverを由来とする保守forkです。
clean-roomで書き直したものではありません。
このライブラリがsourceを持ち、修正も行います。upstreamの変更は内容を読んでから取り込みます。
snapshotを自動で置き換える仕組みはありません。
由来、license、保守の方法は[PROVENANCE.ja.md](../third_party/tinyusb/PROVENANCE.ja.md)にあります。

| 項目 | v3の状態 |
| --- | --- |
| TinyUSB device stack / class driver | 使わない。buildにも含めない |
| DWC2 driver | TinyUSB由来の保守fork（MIT。register定義はST由来のBSD-3-Clause） |
| `tusb_config.h`、`CFG_TUD_*`、`CFG_TUSB_*` | ない。旧flagを定義するとcompile errorになる（`EspUsbLegacyFlags.h`）。0を指定しても拒否する |
| Arduino-ESP32同梱のTinyUSB | 使わない |
| TinyUSBの内部API（`tud_*`など）に依存するコード | v3では動かない |

保守対象のfileがそろっていること、旧vendor経路が残っていないことは、次のcommandで確かめられます。
これはfileの点検で、USBの正しさを証明するものではありません。

```sh
python3 tools/verify_usb_backend.py
```

### 1.3 ESP-IDFから使うもの

| 使うもの | 用途 |
| --- | --- |
| `esp_private/usb_phy.h`（`usb_new_phy`） | PHYの取得と解放 |
| `esp_intr_alloc` | controllerの割込み登録 |
| `esp_cache_msync` | P4のcache同期 |
| `heap_caps_aligned_alloc` | DMA用の内部RAMの確保 |
| FreeRTOS | USB worker、event queue、critical section |

Arduino-ESP32のUSB統合（`USB.h`、`USBCDC`など）は使いません。同じcontrollerを二つのUSB stackで同時に使うことはできません。

---

## 2. taskモデルとcallbackの文脈

### 2.1 USB worker

`begin()`がUSBを開始すると、FreeRTOS taskを1本作ります。これがUSB workerです。

| 項目 | 既定値 | 設定 |
| --- | --- | --- |
| task名 | `espusb-device` | 変更不可 |
| stack | 4096 byte | `EspUsbDeviceConfig::taskStackBytes` |
| priority | `configMAX_PRIORITIES - 1`（最高） | `taskPriority`（-1で既定） |
| core | 固定しない | `taskCoreId`（-1で既定） |
| event queueの深さ | 16 | `eventQueueDepth` |

workerは次の処理をくり返します。

1. event queueを最大1 ms（最低1 tick）待つ。
2. 届いたeventを、最大`eventQueueDepth`件まで処理する。bus resetの処理中は、終わるまで続ける。
3. reset処理が済んでいれば、Functionが登録した内部jobを実行する（NCMの送信など）。

`device.task()`のようなpollは要りません。v3にはその関数もありません。

### 2.2 ISRがすること

controllerの割込み処理（ISR）は、最小限の仕事だけをします。

- registerを読み、eventを作ってqueueへ入れる。
- bus resetの始まり（USBRST）で、coreの世代を進め、構成を無効にする。
- suspend/resumeの状態を更新する。
- ISO OUTが目標のframeを逃したとき、同じbufferのまま偶奇を反転してarmし直す（[8.4](#84-iso-outの取りこぼし)）。

class処理と利用者callbackは、ISRでは実行しません。
現在のclassは、ISRで呼ばれるSOF処理を使っていません。

### 2.3 callbackが呼ばれる場所

ライブラリが呼ぶ利用者callbackは、すべてUSB workerで呼ばれます。

| callbackの例 | 契機 |
| --- | --- |
| `onDirectTxComplete` / `onDirectRxComplete` | 借用転送の完了・取消し・reset・停止 |
| `onRx` / `onRxData` / `onTxComplete` | bulkの受信、copy送信の完了 |
| `onControlRequest`（Vendor） | vendor要求のSETUP/DATA/ACK |
| `onLineCoding` / `onLineState` / `onBreak`（CDC） | CDCの制御要求 |
| HIDの`onOutputReport`など | HostからのOUT/FEATURE report、protocolの変更 |
| MSCの`onRead` / `onWrite`など | SCSI command |
| `onAnyControlRequest`（Device） | 全control要求の観測 |
| Audio、Video、NCM、CCID、DFUのcallback | 各classの契機 |

workerはpriorityが最高です。callbackで時間を使うと、その間USBの応答が止まり、低いpriorityのtaskも動きません。

| callbackの中でしてよいこと | callbackの中でしてはいけないこと |
| --- | --- |
| dataのcopy、flagの設定、queueへの投入、他taskへの通知 | `delay()`、長いloop、完了やtimeoutの待ち |
| 準備済みの次のbufferの投稿（`writeDirect`/`readDirect`） | `device.end()`、`begin()`、`configure()`、callbackの登録 |
| 短いlog | Functionの破棄、大きなheap確保、file I/O |

- callbackに渡されるpointer（`onRxData`のdata、HIDのreportなど）は、callbackの間だけ有効です。残すならcopyします。
- ライブラリは、内部のlockを持ったまま利用者callbackを呼びません。
- `end()`をworkerから呼ぶと`ESP_ERR_INVALID_STATE`で拒否します。
- callbackがblockし続けると、`end()`は`stopTimeoutMs`の後に`ESP_ERR_TIMEOUT`を返します（[3.2](#32-endの順序)）。

### 2.4 どこから何を呼べるか

| 呼出し元 | 呼べるもの |
| --- | --- |
| 通常のtask（`setup()`、`loop()`、自分のtask） | すべての公開API。`begin()`/`end()`/`configure()`/callbackの登録もここから |
| USB worker（callbackの中） | 送受信APIを待たずに使う。借用転送の投稿と取消し、`remoteWakeup()`。CDCの`write()`は待たずに受け付けた分だけ返す。HIDの`sendHidReport`は`timeoutMs = 0`で |
| ISR | 公開APIを呼ばない。task通知などでtaskへ渡す |

- ISRからの呼出しには対応していません。`remoteWakeup()`は`ESP_ERR_INVALID_STATE`、`remoteWakeupEnabled()`はfalse、CDCの`write()`は0を返します。他のAPIは結果を保証しません。
- 同じDeviceに対する設定とlifecycleの操作（`configure()`、`begin()`、`end()`、callbackの登録）は、1つのtaskにまとめてください。ライブラリはこれらを互いに排他しません。
- 投稿したAPIの中で、callbackを直接呼ぶことはありません。ただしworkerが別のcoreで動いていると、投稿関数が戻る前に完了callbackが走ることがあります。
  callbackが使うcontextは投稿の前に準備してください。戻り値の`id`を変数へ代入し終えていることに頼らないでください。

### 2.5 event queueとあふれ

ISRが作ったeventは、`eventQueueDepth`件のqueueでworkerへ渡ります。
この件数とは別に、hardware event用の予備を1件確保します。

workerが長く止まるとqueueがあふれます。このとき、ライブラリは黙ってeventを捨てません。
完了eventを捨てると、借用bufferが永久に戻らなくなるからです。代わりに次の手順で回復します。

1. 割込みを止め、Hostから切断し、全endpointのDMAを止める。
2. 各classを初期化し、借用bufferを返す（`BusReset`）。`onBusDetached`も呼ぶ。
3. queueに残った古いeventを捨てる。
4. 約20 ms待ってから接続し直す。Hostは再列挙します。

利用者から見ると「一度抜けて、また挿さった」ように見えます。
これが起きるなら、callbackが重いか、workerのpriorityが低すぎます。`eventQueueDepth`を増やすのは最後の手段です。

### 2.6 workerの設定

```cpp
EspUsbDeviceConfig config;
config.taskStackBytes = 8192;   // callbackでprintfや大きなlocal変数を使うなら増やす
config.taskPriority = -1;       // 既定：最高
config.taskCoreId = 0;          // 既定：-1（固定しない）
config.eventQueueDepth = 16;    // 既定：16。0は不可
config.stopTimeoutMs = 5000;    // 既定：5000。end()がworkerの停止を待つ時間。0は期限なし
if (!device.begin(config)) Serial.println(device.lastErrorName());
```

| 設定 | 制約 | 違反したとき |
| --- | --- | --- |
| `taskStackBytes` | byte単位。FreeRTOSの最小値以上 | `ESP_ERR_INVALID_SIZE` |
| `taskPriority` | -1、または0〜`configMAX_PRIORITIES - 1` | `ESP_ERR_INVALID_ARG` |
| `taskCoreId` | -1、または実在するcore番号（single coreでは0だけ） | `ESP_ERR_INVALID_ARG` |
| `eventQueueDepth` | 1以上（`uint16_t`） | `ESP_ERR_INVALID_SIZE`。大きすぎると`ESP_ERR_NO_MEM` |

- 検証はPHYを取る前に行います。
- 利用者callbackはworkerのstackで動きます。stackを小さくする、priorityを下げる場合は、callbackの重さを見積もってください。
- priorityを下げると、他のtaskがUSBの応答を遅らせます。queueのあふれと再列挙（[2.5](#25-event-queueとあふれ)）の原因になります。
- coreの固定は、同じcoreで重いtaskと競合する場合に効くことがあります。効果は構成によるので、測ってから決めてください。
- 設定は`begin()`の時に読みます。変えるには`end()`の後、次の`begin()`で渡します。
- `DescriptorsOnly`ではworkerを作らないので、これらの値は使いません（範囲の検証もしません）。

---

## 3. 開始と停止

### 3.1 begin()の順序

`begin()`は、検証と確保を全部終えてから、最後にHostへ接続します。
途中で失敗したら、取得したものを逆順に戻して`false`を返します。理由は`lastError()`で読めます。

| 段階 | 内容 | 主な失敗 |
| --- | --- | --- |
| 1. 状態の確認 | 開始済みでないか。別のDeviceが動いていないか。登録を拒否されたFunctionが残っていないか。Functionが251個以下か。`startMode`が正しいか | `INVALID_STATE`、`INVALID_SIZE`、`INVALID_ARG` |
| 2. 設定の確認 | `deviceInterfaceGuid`の形式 | `INVALID_ARG` |
| 3. Function一覧 | 登録順のpointer一覧を、実数分だけ確保 | `NO_MEM` |
| 4. descriptorの生成 | 各Functionの設定の検証、番号の割当て、configuration（FS、HS）、文字列、BOS/MS OS 2.0。endpoint数の上限も検査（[4章](#4-descriptorの生成)） | `INVALID_SIZE`、`INVALID_ARG`、`NO_MEM` |
| 5. FIFOの予算 | bulk INの二重buffer（[6.4](#64-bulk-inの二重buffer)）。全IN endpointの送信FIFOの予算（[6.5](#65-送信fifoの予算)） | `INVALID_SIZE` |
| 6. Functionの開始 | 登録順に各Functionの開始処理。ring、DMA用buffer、class driverの登録 | 各Functionの理由 |
| 7. USBの開始 | worker設定の検証 → PHYの取得 → controllerの設定 → coreの開始 → workerの作成 | `INVALID_ARG`、`INVALID_SIZE`、`NOT_SUPPORTED`、`NO_MEM`、`FAIL` |
| 8. 開始後の処理 | 各Functionの開始後の処理 | `FAIL` |

Functionの開始・停止の処理（`begin()`、`afterDeviceStarted()`、`prepareToStop()`）はprotectedです。`device.begin()`／`device.end()`だけが呼びます。sketchから`console.begin()`のように呼ぶとcompile errorになります。

段階7の「coreの開始」は、さらに次の順に進みます。

1. 選んだ構成からinterface数、endpoint番号、方向を数える。
2. core状態、endpoint表、interface表、event queue、EP0用の64 byte、DWC2の転送recordを、**1つの内部DMA領域**にまとめて確保する。
3. EP0のSETUP領域をcache同期する（P4）。失敗したら`ESP_FAIL`。
4. class driverを初期化し、controllerを初期化する。
5. 割込みを登録して有効にする。登録に失敗したら`ESP_FAIL`。

段階1〜6で失敗した場合、PHY、割込み、workerは一度も動きません。Hostからは何も起きないように見えます。
「挿しても無反応」なら、まず`begin()`の戻り値と`lastErrorName()`を見てください。

`begin()`の成功は「列挙に成功した」という意味ではありません。Hostが構成を済ませたかは`ready()`で見ます。

### 3.2 end()の順序

`end()`は、新しい受付を閉じてから、DMAを止め、bufferを返し、資源を解放します。

1. workerから呼ばれていたら`ESP_ERR_INVALID_STATE`で拒否する。
2. 各Functionの停止準備。新しい投稿を受け付けなくする。
3. workerへ停止を伝え、workerが終わるまで`config.stopTimeoutMs`（既定5000 ms、0は期限なし）待つ。workerは次を行ってから終わる。
   1. 割込みを止め、Hostから切断する。
   2. controllerを止める。進行中のDMAを止める。
   3. 各class driverを終了する。借用bufferは`Stopped`で返す（このcallbackもworkerで呼ぶ）。
   4. 構成済みだったら`onBusDetached`を呼ぶ。
   5. 割込みの登録を解放し、coreの領域を解放する。
4. PHYを解放する。
5. 各Functionの`end()`を登録の逆順に呼ぶ。ringなどを解放する。
6. descriptorとFunction一覧を解放する。

`end()`が`ESP_OK`で戻った後は、callbackもDMAもbufferに触れません。bufferやFunctionを破棄してよくなります。

- callbackがblockしてworkerが期限内に終わらないと、`end()`は`ESP_ERR_TIMEOUT`を返します。手順4以降は行いません。停止の要求は残るので、callbackが戻った後にもう一度`end()`を呼ぶと停止が終わります。
- DeviceとFunctionのdestructorは、期限なしで停止の完了を待ちます。破棄した領域をworkerが使い続けないためです。
- `end()`の後、同じobjectで`configure()`をやり直し、もう一度`begin()`できます。
- 稼働中にFunctionを破棄すると、destructorがDeviceを止めます。通常は`end()`→Functionの破棄→Deviceの破棄の順にします。

### 3.3 資源の確保と解放

| 資源 | 確保する時 | 解放する時 | 置き場所 |
| --- | --- | --- | --- |
| Function一覧 | `begin()`（または停止中のdescriptor照会） | `end()`、開始失敗 | heap |
| descriptor（configuration、HID report、BOS、MS OS 2.0、文字列、WebUSB URL） | `begin()`。使うものだけ、実際の長さで | `end()`、開始失敗 | heap |
| classのring | 各Functionの開始（`device.begin()`の中）。利用者が`rxBuffer`などを渡せばそれを借りる | `end()` | 自動確保はheap。利用者bufferはPSRAMも可 |
| classのDMA用buffer、copy送信のstaging | 各Functionの開始（`device.begin()`の中） | `end()` | 内部RAM（DMA可能、64 byte境界） |
| core領域（queue、EP0、DWC2 record） | USBの開始 | `end()` | 内部RAM（DMA可能、1つの領域） |
| PHY、割込み、worker | USBの開始 | `end()` | ESP-IDF |

- 転送の途中、取消し、queueあふれの回復では、heapを確保しません。
- DWC2の転送recordは、全alternateのdescriptorに現れるendpoint方向と、EP0の2方向の分だけ作ります。使わない方向の分は作りません。
- Functionを1つも登録しない`begin()`は、controllerを起動せず、core領域も確保しません。このとき`started()`はtrue、`ready()`はfalseです。

### 3.4 状態の照会

| 関数 | trueの条件 |
| --- | --- |
| `started()` | `begin()`が成功してから`end()`まで。資源を持っている。接続待ちやreset中もtrue |
| `ready()` | HostがSET_CONFIGURATIONを済ませている |
| `suspended()` | busが休止している。休止中も`ready()`はtrueのまま |

`speed()`は、Hostが最後のbus resetで決めた速度（`EspUsbBusSpeed::Full`／`High`）を返します（USB 2.0 §7.1.7.5）。USB開始前、`DescriptorsOnly`、`end()`後、最初のbus resetの前は`None`です。HS controllerでも、FSのHostやhubにつなぐと`Full`です。

照会してから次の操作までに、busの状態は変わります。`ready() && !suspended()`は、次の転送の成功を約束しません。
転送の結果は、各APIの戻り値と完了通知で判断してください。

### 3.5 DescriptorsOnlyでの事前検証

`startMode = DescriptorsOnly`にすると、PHY、割込み、workerを使わずに`begin()`を通せます。
Hostもcableも要りません。自分の構成を自動試験したいときに使います。

```cpp
EspUsbDeviceConfig config;
config.startMode = EspUsbDeviceStartMode::DescriptorsOnly;
if (!device.begin(config)) {
  Serial.println(device.lastErrorName());   // 構成の誤りはここで分かる
  return;
}
const uint8_t *fs = device.configurationDescriptorForSpeed(0, false);
const uint8_t *hs = device.configurationDescriptorForSpeed(0, true);
// ここでbyte列を読む・copyする。pointerはend()まで有効
device.end();
```

| 検証すること | 検証しないこと |
| --- | --- |
| 各Functionの設定、descriptorの長さと内容、文字列とURL | PHY、割込み、DMA、cacheの動作 |
| endpoint番号と非control IN endpointの数 | Hostとの列挙、標準要求の応答 |
| bulk IN二重bufferと送信FIFOの予算 | workerの設定（stack、priority、core、queue） |
| 各Functionのring/DMA bufferの確保 | 実際の転送 |

- P4（`controller = Auto`）では、HS controllerの予算で検証します。
- S2/S3でもHS用のdescriptorを照会できます（`configurationDescriptorForSpeed(0, true)`）。予算はFS controllerの物理的な値のままです。
- USBを動かしている間、FS専用の機種ではHSの照会は`nullptr`を返し、`lastError()`は`ESP_ERR_NOT_SUPPORTED`です。
- 停止中にdescriptorを照会すると、必要なら作り直します。受け取ったpointerを、次の照会や構成変更をまたいで持たないでください。

[`EspUsbDeviceDescriptorDump`](../examples/Info/EspUsbDeviceDescriptorDump/)はこの方法で全descriptorを表示します。

---

## 4. descriptorの生成

USBの多byte値は、すべてlittle endianです。この章では「どのfieldを利用者が決め、どれをライブラリが決めるか」を示します。
規格はUSB 2.0 §9.6（Standard USB Descriptor Definitions）です。

### 4.1 Functionの登録と番号の割当て

Functionはconstructorで、Deviceの登録listに入ります。登録にheapは使いません。

- 登録の順番は保たれます。Function数の上限は251です（文字列indexが5〜255のため）。
- 稼働中の登録は`ESP_ERR_INVALID_STATE`で拒否します。拒否されたFunctionが残ったままだと、次の`begin()`も拒否します。
  停止後に`device.addClass(&function)`で登録し直すか、作り直してください。
- DeviceはFunctionを所有しません。DeviceはFunctionより長く生かします。DeviceもFunctionもcopy/moveできません。

interface番号とendpoint番号は、`begin()`が次の規則で割り当てます。手で決めることはできません。

1. interface番号は0から、endpoint番号は1から数える。
2. HIDを先に置く。HID Functionが2つ以上なら、1つのHID interface（interface 0、endpoint 1のINとOUT）にまとめる（[4.5](#45-hid-reportの結合)）。
3. 次に、HID以外のFunctionを登録順に置く。
4. 各Functionは、自分の`interfaceCount()`個のinterface番号と、`endpointCount()`個の連続したendpoint番号を受け取る。INとOUTは同じ番号を使えます。

| Function | interface | endpoint番号 | 非control IN |
| --- | --- | --- | --- |
| HID（1つ） | 1 | 1 | 1 |
| HID（2つ以上をまとめたもの） | 1 | 1 | 1 |
| CDC ACM | 2 | 2（通知IN、data IN/OUT） | 2 |
| Vendor | 1 | 1（IN/OUT） | 1 |
| MIDI | 2 | 1（IN/OUT） | 1 |
| MSC | 1 | 1（IN/OUT） | 1 |
| NCM | 2 | 2（通知IN、data IN/OUT） | 2 |
| CCID | 1 | 1（IN/OUT）。`removable`なら2（通知INを追加） | 1または2 |
| Video | 2 | 1 | 1 |
| DFU | 1 | 0 | 0 |
| Audio | 構成による | 構成による | 構成による |

登録の順番を変えると、番号も変わります。Host側のprogramはendpoint addressを決め打ちせず、descriptorから読んでください。

### 4.2 device descriptor（18 byte）

| offset | field | 値・誰が決めるか |
| --- | --- | --- |
| 0 | bLength | 18 |
| 1 | bDescriptorType | 0x01 |
| 2 | bcdUSB | 0x0200。BOSを出すときは0x0201 |
| 4 | bDeviceClass | IADがあれば0xEF、なければ0x00 |
| 5 | bDeviceSubClass | IADがあれば0x02、なければ0x00 |
| 6 | bDeviceProtocol | IADがあれば0x01、なければ0x00 |
| 7 | bMaxPacketSize0 | 64（FS/HSとも） |
| 8 | idVendor | `config.vid`（既定0x303A） |
| 10 | idProduct | `config.pid`（既定0x4000） |
| 12 | bcdDevice | `config.deviceVersion`（既定0x0100） |
| 14 | iManufacturer | `config.manufacturer`があれば1 |
| 15 | iProduct | `config.product`があれば2 |
| 16 | iSerialNumber | `config.serialNumber`があれば3 |
| 17 | bNumConfigurations | 1 |

- **bcdUSBが0x0201になるのは、BOSを出すときだけです。** HostはbcdUSBが0x0201以上のときにBOSを要求します。
  0x0210にすると、実装していないUSB 2.1の機能を主張することになるので使いません。
- **bDeviceClassはIADの有無で決まります。** configurationの中にIAD（type 0x0B）が1つでもあれば、0xEF/0x02/0x01を立てます。
  これはUSB-IFのIAD ECN（Interface Association Descriptors）の要求です。
  CDC、NCM、Audio、VideoはIADを出します。HID、MSC、Vendor、MIDI、CCID、DFUだけの構成では0x00のままです。
- VID/PIDの既定値は試作用です。製品では自分のVID/PIDを使ってください（[入門編](usb-device-guide.ja.md)）。

### 4.3 configuration descriptor（9 byte＋続き）

| offset | field | 値・誰が決めるか |
| --- | --- | --- |
| 2 | wTotalLength | 実際の長さ（上限はUSBの16 bit） |
| 4 | bNumInterfaces | 割り当てたinterfaceの数 |
| 5 | bConfigurationValue | 1 |
| 6 | iConfiguration | 0 |
| 7 | bmAttributes | bit7=1、bit6=`config.selfPowered`、bit5=`config.remoteWakeupSupported` |
| 8 | bMaxPower | `config.maxPowerMilliamps`を2 mA単位へ切り上げ。上限250（500 mA） |

続いて、IAD、interface、endpoint、class固有descriptorが並びます。
読み方は「先頭byteが長さ、2 byte目が型」のくり返しだけです。

v3は長さを先に数え、その長さだけ確保して書きます。固定の上限（v2の704 byte）はありません。
長さを数える処理と書く処理は同じ構成から作ります。食い違えば`begin()`が`ESP_ERR_INVALID_SIZE`で拒否します。

### 4.4 interfaceとendpoint

endpoint descriptor（7 byte）で設計に関わるのは、次のfieldです。

| offset | field | 備考 |
| --- | --- | --- |
| 2 | bEndpointAddress | bit7=方向（1=IN）、bit3..0=番号。ライブラリが割り当てる |
| 3 | bmAttributes | 転送の種類。classが決める |
| 4 | wMaxPacketSize | 速度とclassで決まる（[7.1](#71-最大packet長)） |
| 6 | bInterval | polling間隔の要求。保証ではない |

bIntervalの意味はUSB 2.0 §9.6.6に従います。

| 速度・種類 | bIntervalの解釈 | ライブラリが受け付ける範囲 |
| --- | --- | --- |
| FS interrupt | そのままms | 1〜255 |
| FS isochronous | 2^(bInterval-1) frame | 1〜16 |
| HS interrupt / isochronous | 2^(bInterval-1) microframe（125 µs） | 1〜16 |
| bulk | 使わない | 無視 |

範囲外の値は、endpointを開く前（FIFOを確保する前）に拒否します。
実際のpolling間隔を決めるのはHostです。小さい値を書いても、Hostの都合とbusの混み具合には勝てません。

### 4.5 HID reportの結合

HID Functionが2つ以上あると、1つのHID interfaceにまとめます。
interfaceは増えず、report IDが増えます。endpointはIN 1本とOUT 1本です（endpoint 1）。

report descriptorは、各Functionのdescriptorを次の形で連結します。

```text
[Push] [Functionのitem … 最初のCollectionの直後に Report ID(0x85, id) … 残り] [Pop]
```

- Push/Popで囲むので、あるFunctionのglobal item（単位、範囲、report size）が次のFunctionへ漏れません。
- report IDは、まず各Functionが希望するID（`hidReportId()`）を予約し、重なったものと希望のないものに空いているIDを割り当てます。
  v2のような型ごとの固定IDではありません。実際のIDは`compositeReportId()`で読めます。
- 結合したdescriptorの長さに固定の上限はありません（USBの16 bitまで）。
- まとめたHIDのendpoint MPSは、既定16 byteです。各Functionが要求する大きさの最大を取ります。`EspUsbDeviceHidCustom`の`inputReportSize`と`EspUsbDeviceHidVendor`のreport長も反映します（FSは64、HSは1024 byteまで）。
- 実際に決まったMPSは、各HID Functionの`inputPacketSize()`で読めます。Hostが速度を決めてからの値で、bus resetの前と`end()`の後は0です。

HID descriptor（type 0x21、9 byte）には、report descriptorの長さ（wDescriptorLength、offset 7〜8）だけが入ります。
中身は、Hostが`GET_DESCRIPTOR(0x22)`で別に読みます。詳しくは[HID](classes/hid.ja.md)を参照してください。

### 4.6 文字列descriptor

| index | 内容 |
| --- | --- |
| 0 | 言語ID。0x0409だけ |
| 1〜3 | `config.manufacturer` / `product` / `serialNumber` |
| 4 | 予約 |
| 5〜 | Function名（登録順に1つずつ） |
| その後 | Function固有の補助文字列（NCMのMAC addressなど） |

- 入力はNUL終端のUTF-8です。`begin()`がUTF-16LEへ変換してcopyします。成功した後は、元の配列を変えても応答は変わりません。
- 1文字列は126 UTF-16 code unitまで（headerを含めて254 byte）。超えると`ESP_ERR_INVALID_SIZE`です。
- 不正なUTF-8（overlong、surrogate、U+10FFFF超、途中で切れた列）は`ESP_ERR_INVALID_ARG`です。置換や切り詰めはしません。
- `nullptr`を渡した項目は省略します（indexは0）。空文字は長さ0の文字列descriptorになります。Function名の空文字は省略です。
- 文字列をすべて省略すれば、文字列用のheapは確保しません。
- 0x0409以外のLANGIDの要求には、STALLで答えます。

### 4.7 BOS、WebUSB、Microsoft OS 2.0

BOSは、WebUSBかMicrosoft OS 2.0を使うときだけ出します。どちらも使わなければ、pointerも長さも0です。

| 条件 | BOSの中身 |
| --- | --- |
| `config.webusbEnabled = true` | WebUSB platform capability（24 byte） |
| Vendor/DFU Functionがある | Microsoft OS 2.0 platform capability（28 byte） |

BOSの長さは、5 byte＋上の合計（29、33、57 byte）です。

**Microsoft OS 2.0 descriptor set**は、Windowsが.infなしでWinUSBを当てるための情報です。
Vendor/DFUのinterfaceがあるときに作ります。

| 要素 | 内容 |
| --- | --- |
| compatible ID | `WINUSB`。Vendor/DFUの各interfaceに1つ |
| registry property | `DeviceInterfaceGUIDs`。Vendorだけに付ける。値は`config.deviceInterfaceGuid` |
| vendor revision | `config.msOs20VendorRevision`。0（既定）ならset全体のCRCから作る |
| CCGP device | `config.msOs20CcgpDevice = true`のとき、Windowsに複合deviceとして扱わせる |

setの形は2通りあります。

```text
flat（interfaceが1つ）              subsets（interfaceが2つ以上）
Set header (10)                    Set header (10)
  Vendor revision (6)                Configuration subset (8)
  Compatible ID (20)                   Function subset (8, bFirstInterface)
  Registry property (132)                Vendor revision (6)
                                         Compatible ID (20)
                                         Registry property (132)
                                       Function subset (8, bFirstInterface)
                                         …
```

| `msOs20Layout` | 結果 |
| --- | --- |
| `ESP_USB_DEVICE_MS_OS_20_AUTO`（既定） | interfaceが2つ以上、または`msOs20CcgpDevice`ならsubsets。それ以外はflat |
| `ESP_USB_DEVICE_MS_OS_20_FLAT` | 常にflat。WinUSB対象が2つ以上あると`begin()`が`ESP_ERR_INVALID_ARG` |
| `ESP_USB_DEVICE_MS_OS_20_SUBSETS` | 常にsubsets |

形を間違えると、driverが当たりません。理由は次のとおりです。

- function subsetは、複合deviceの1機能を指すための入れ子です。Windowsは、複合deviceにだけ読み込む`usbccgp.sys`を通して解決します。
- 単一interfaceのdeviceにsubsetを付けると、compatible IDの行き先がありません。driverが当たらず、Device Managerはcode 28を出します。
- 複合deviceで一部のinterfaceしか指さないと、指されなかったinterfaceはdriverなしのまま残ります。

**`deviceInterfaceGuid`は、Hostのapplicationがdeviceを探すときのGUIDです。** どのdriverが当たるかは、compatible IDだけで決まります。
既定値はこのライブラリの全Vendor interfaceで共通なので、製品では自分のGUIDを付けてください。
形式は`{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}`（波括弧を含めて38文字）です。違えば`begin()`が`ESP_ERR_INVALID_ARG`です。

**vendor revisionは、Windowsに読み直させるための値です。** Windowsは、最初に列挙したときのregistry propertyをVID/PID/serialごとに覚え、この値が変わったときだけ読み直します。
既定ではset全体から計算するので、GUIDやFunctionを変えると自動で変わります。実際の値は`microsoftOs20VendorRevision()`で読めます。

**WebUSB**の動作：

| `webusbUrl` | 動作 |
| --- | --- |
| `nullptr` | landing pageなし（iLandingPage=0） |
| `https://…` | scheme 1。prefixを除いて保存 |
| `http://…` | scheme 0。prefixを除いて保存 |
| それ以外 | scheme 255。全文をそのまま保存 |

prefixを除いて最大252 byteのUTF-8です。空、prefixだけ、不正なUTF-8は`ESP_ERR_INVALID_ARG`、長すぎると`ESP_ERR_INVALID_SIZE`です。

**ライブラリが使うvendor code：**

| bRequest | 用途 | 注意 |
| --- | --- | --- |
| 0x01 | WebUSB（bVendorCode） | `webusbEnabled`のとき、GET_URL（bmRequestType 0xC0、wIndex 2）だけをライブラリが受け取る。wValueがURLの番号1でなければSTALL |
| 0x02 | Microsoft OS 2.0（bMS_VendorCode） | setがあるとき、bmRequestType 0xC0、wValue 0、wIndex 7（MS_OS_20_DESCRIPTOR_INDEX）の要求はライブラリが答える |

上の組合せに当たらない要求は、bRequestが同じでも、Vendorの`onControlRequest`へ届きます。上の組合せそのものは届きません。

### 4.8 Device QualifierとOther Speed Configuration

USB 2.0 §9.6.2と§9.6.4の扱いです。

| 機種・設定 | Device Qualifier | Other Speed Configuration |
| --- | --- | --- |
| S2 / S3（FS専用） | 返さない（STALL） | 返さない（STALL） |
| P4、`controller = FullSpeed` | 返さない（STALL） | 返さない（STALL） |
| P4、`controller = Auto` / `HighSpeed` | 10 byte | 今の速度でない方のconfiguration。typeを0x07にしたもの |

- FS専用のdeviceは、Device Qualifierの要求にrequest error（STALL）で答えると規格が定めています（§9.6.2）。
- HS対応のcontrollerでは、`begin()`がFS用とHS用の両方のconfigurationを作ります。Other Speed用の領域（大きい方の長さ）も先に確保します。
- Device Qualifierは、bcdUSB、class code、bMaxPacketSize0（64）、bNumConfigurations（1）をdevice descriptorと合わせます。

### 4.9 複数機能のdeviceをWindowsにどう見せるか

Linux/macOSは、configurationの中のIADを見て、機能ごとにdriverを当てます。Windowsはdevice descriptorも見ます。

1. bDeviceClassが0xEF/0x02/0x01なら、`usbccgp.sys`が親としてloadされる。
2. usbccgpが、IADの区切りごとに子deviceを作る。
3. 子deviceごとにclassのdriverが当たる。CDC ACMなら`usbser.sys`で、1機能に1つのCOM portになる。

ライブラリは、IADを出したときに1.を自動で行います（[4.2](#42-device-descriptor18-byte)）。残りは設計側の仕事です。

| すること | 理由 |
| --- | --- |
| Functionに名前を付ける（`EspUsbDeviceCdcSerial cdc(device, "Console")`） | WindowsはIADのiFunctionを子deviceの名前に使う。ないと同じ名前が並ぶ |
| `config.serialNumber`をboardごとに固有の値にする | WindowsはVID/PID/serialでCOM port番号を覚える。ないと、挿すportを変えるたびに番号が変わる |
| 開発中にdescriptorを変えたら、PIDも変える | WindowsはVID/PIDごとにdriverの結果を覚えている。古い結果が残ると「portが1つしか出ない」などになる |

この節のWindowsの挙動は、v2で実測したものです。v3のdescriptorは同じ構造ですが、v3でのWindows実機確認はしていません。

---

## 5. control転送

### 5.1 SETUP packet（8 byte）

| byte | field | 内容 |
| --- | --- | --- |
| 0 | bmRequestType | bit7=方向（1=IN）、bit6..5=種類（0:標準 1:class 2:vendor）、bit4..0=宛先（0:device 1:interface 2:endpoint） |
| 1 | bRequest | 要求の番号 |
| 2–3 | wValue | 要求ごとの意味 |
| 4–5 | wIndex | interface番号やendpoint address |
| 6–7 | wLength | data段階のbyte数 |

### 5.2 EP0の処理

control転送は、SETUP →（DATA）→ STATUSの3段階です（USB 2.0 §8.5.3）。coreが段階を管理します。

- EP0のMPSは64 byteです。data段階は64 byteずつ、coreの内部bufferを通してcopyします。
- 新しいSETUPが来たら、前の要求は捨てます。SETUPの通し番号とbusの世代で区別するので、古いdataが新しい要求に混ざりません。
- 要求に答えられないときはSTALLします。STALLは故障ではなく、「その要求には対応しない」という正しい答えです。

### 5.3 標準要求の扱い

標準要求はcoreが処理します。方向、値、index、長さを検査し、合わない要求はSTALLします。

| 要求 | 扱い |
| --- | --- |
| GET_STATUS（device） | self-powered bitと、remote wakeupの許可bitを返す |
| SET_FEATURE / CLEAR_FEATURE（DEVICE_REMOTE_WAKEUP） | remote wakeupを広告していて、Address状態かConfigured状態のときに受け付ける（USB 2.0 §9.4.1、§9.4.9） |
| SET_FEATURE（TEST_MODE） | HSで動いているときだけ受け付ける。test selector（wIndexの上位byte）は1〜4（Test_J、Test_K、Test_SE0_NAK、Test_Packet）。STATUS段階の後に入り、電源を入れ直すまで抜けない（USB 2.0 §7.1.20、§9.4.9）。FSと、それ以外のselectorはSTALL |
| SET_ADDRESS | 構成済みのときは拒否する |
| GET_DESCRIPTOR | device、configuration、string、device qualifier、other speed、BOS（[4章](#4-descriptorの生成)） |
| GET_CONFIGURATION / SET_CONFIGURATION | 値は0か1。SET_CONFIGURATIONは同じ値でも全endpointを閉じ、classを初期化し直す |
| GET_INTERFACE / SET_INTERFACE | classに渡す。classが扱わなければ、alternate 0だけを受け付ける |
| GET_STATUS（endpoint） | halt bitを返す |
| SET_FEATURE / CLEAR_FEATURE（ENDPOINT_HALT） | EP0とisochronous endpointは拒否。それ以外は実行してから、classへ知らせる |

- SET_CONFIGURATIONで構成を解除・再選択すると、DMAを止め、借用bufferを返し（`BusReset`）、classを初期化します。
  未対応の値の要求は拒否しますが、動いている構成は解除しません。
- SET_INTERFACEやENDPOINT_HALTで転送がどうなるかは、classごとに違います（[9.5](#95-reset標準要求endとの関係)と各class文書）。

### 5.4 class要求とvendor要求の行き先

| 要求 | 行き先 |
| --- | --- |
| class要求、宛先interface | wIndexの下位byteのinterfaceを持つFunction |
| class要求、宛先endpoint | そのendpointを持つFunction |
| vendor要求（宛先によらず） | まずライブラリ（WebUSB、MS OS 2.0）、次に各Functionの順 |

Vendor Functionは、`onControlRequest`でvendor要求を受け取ります。callbackはSETUP、DATA、ACKの各段階で呼ばれます。

```cpp
vendor.onControlRequest([](const EspUsbDeviceVendorControlRequest &r) {
  if (r.bmRequestType == 0xC0 && r.bRequest == 0x10) {         // IN、vendor、device宛
    if (r.stage == ESP_USB_DEVICE_CONTROL_STAGE_SETUP)
      return vendor.sendControlResponse(r, info, sizeof(info)); // wLengthで切り詰められる
    return true;
  }
  return false;   // STALL
});
```

callbackの登録は、停止中に行います。詳しくは[Vendor](classes/vendor.ja.md)を参照してください。

### 5.5 応答の規則

| 状況 | 動作 |
| --- | --- |
| INで、要求長より短い応答 | 正常。short packetでdata段階を終える |
| INで、要求長より長い応答 | wLengthで切り詰める |
| INで、要求長が1以上、応答の長さが0 | 長さ0のdata段階（ZLP）を送る |
| 要求長が0（no-data） | STATUS段階だけを返す |
| OUTで、受信先が要求長より小さい | 拒否する |

要求長が1以上のINに長さ0で答えるとき、STATUSへ飛ばずにZLPを送ります。Hostがdata段階を待ち続けないためです。

### 5.6 要求を観測する

`EspUsbDevice::onAnyControlRequest()`で、ライブラリが自分で答えた要求も含めて観測できます。
callbackは観測だけで、deviceの応答は変わりません。

```cpp
device.onAnyControlRequest([](const EspUsbDeviceControlRequestInfo &r) {
  // USB workerで呼ばれる。counterやqueueに留める
});
```

| 段階 | 報告される要求 |
| --- | --- |
| `SETUP` | すべてのvendor要求（ライブラリが答えるかどうかに関係なく） |
| `ACK` | STATUS段階まで終わったすべてのcontrol転送 |

| field | 意味 |
| --- | --- |
| `responseLength` | ライブラリが用意した応答の長さ（wLengthで切り詰める前）。descriptor、WebUSB URL、MS OS 2.0 setのときだけ0以外 |
| `handled` | SETUPでは「ライブラリが認識して答えた」、ACKでは「転送が終わった」 |

STALLした要求は、STATUS段階に届きません。vendor要求をSETUPで報告するのは、「来たが断った」と「来ていない」を区別するためです。
例えば「WindowsがMS OS 2.0 setを要求しているか」は、`type=0xC0 req=0x02 idx=0x0007`が出るかで分かります。

登録は停止中に行ってください。`onAnyControlRequest()`は`esp_err_t`を返し、稼働中は`ESP_ERR_INVALID_STATE`で拒否します（workerが参照しているため）。

---

## 6. endpoint番号とFIFO

### 6.1 controllerの能力表

数値は`src/internal/EspUsbDwc2Platform.cpp`の`espusb_dwc2_ports`です。
descriptorの検査、bulk IN二重buffer、送信FIFOの予算は、すべてこの表を使います。

| SoC / controller | 速度 | endpoint番号（EP0を除く） | 非control IN | 非control OUT | FIFO（32 bit word） |
| --- | --- | --- | --- | --- | --- |
| ESP32-S2 / S3 | FS | 1〜6 | 4本 | 6本 | 256 |
| ESP32-P4 FS controller | FS | 1〜6 | 4本 | 6本 | 256 |
| ESP32-P4 HS controller | HS | 1〜15 | 7本 | 15本 | 1024 |

上限を超えると、`begin()`が`ESP_ERR_INVALID_SIZE`で拒否します。PHY、割込み、workerは動きません。
1つのendpoint番号の同じ方向を、別々のinterfaceで使うこともできません。

### 6.2 IN endpointが先に尽きる

DWC2では、IN endpointごとに専用の送信FIFOが要ります。OUTは共有の受信FIFOを使います。
そのため、S2/S3とP4 FSでは、非control INの4本が先に上限になります。

例（S3）：

| 構成 | IN | 結果 |
| --- | --- | --- |
| HID + CDC + MSC | 1 + 2 + 1 = 4 | 入る |
| CDC + CDC | 2 + 2 = 4 | 入る |
| CDC + CDC + CDC | 6 | `INVALID_SIZE` |
| CDC + Vendor + HID + MSC | 2 + 1 + 1 + 1 = 5 | `INVALID_SIZE` |

減らし方：

1. 単にbyte列を流すだけなら、CDC（IN 2本）でなくVendor（IN 1本）にする。
2. HIDをまとめる。複数のHID Functionは、1つのinterfaceとIN 1本にまとまる。
3. P4のHS controllerを使う（IN 7本）。

### 6.3 FIFOの配分

DWC2は、小さなFIFO SPRAMを次の用途で分け合います。

| 用途 | 大きさ（word） |
| --- | --- |
| DMAのendpoint情報 | endpoint数 × 2（S2/S3/P4 FSは14、P4 HSは32） |
| 受信FIFO | 14 + 2 × (最大OUT MPS / 4 + 1) + endpoint数 × 2 |
| EP0の送信FIFO | 16（64 byte） |
| IN endpointごとの送信FIFO | ceil(MPS / 4)。bulk INの二重bufferなら2倍 |

- 送信FIFOの番号は、USBのendpoint番号ではなく、使っているIN endpointの中での順位です。EP0は常にFIFO 0です。
  endpoint番号に空きがあっても、FIFOは詰めて割り当てます。
- FIFOは、SET_CONFIGURATIONでendpointを開くたびに割り当てます。`begin()`の直後のregisterは、割当て前の値です。

S3で、最大OUT MPSが64 byteの構成の例：256 − 14（DMA情報）− 16（EP0 IN）− 62（受信FIFO）= 164 word（656 byte）がIN endpointに残ります。
そのため、FSのisochronous INを上限の1023 byteでは取れません。

P4 HSの例：Vendor（bulk IN 512 byte＝128 word）は5個まで入ります。6個は非control INの本数（7本）には収まりますが、受信FIFO、EP0とあわせると1024 wordを超えるので、`begin()`が`ESP_ERR_INVALID_SIZE`で拒否します。

### 6.4 bulk INの二重buffer

送信FIFOが1 packet分だと、今のpacketが出ていくまで次を用意できません。2 packet分あれば、1つを送りながら次を用意できます。
代償はRAMではなく、controller内の固定のFIFOです。足りないとendpointが開かず、列挙に失敗します。

`config.bulkInBuffering`で選びます。

| 値 | 動作 |
| --- | --- |
| `Auto`（既定） | **全部の**bulk INを2 packet分にしても収まるときだけ2倍にする。収まらなければ何も変えない |
| `Single` | 1 packet分のまま |
| `Double` | 全部を2倍にする。収まらなければ`begin()`が`ESP_ERR_INVALID_SIZE` |

「全部か無しか」にしているのは、一部だけ2倍にする規則だと、速度がFunctionの登録順で変わってしまうからです。

| controller | 目安 |
| --- | --- |
| S2 / S3 / P4 FS | 64 byteのbulk INなら、4本とも2倍にできる |
| P4 HS | 512 byteのbulk INは、2本まで2倍にできる。3本以上はAutoでは2倍にしない |

実際に2倍にしたendpointのbitmapは、`device.bulkInDoubleBuffered()`で読めます。streamが思ったより遅いときに、最初に見る値です。

### 6.5 送信FIFOの予算

`begin()`は、構成にある全IN endpointについて、受信FIFO、EP0、各IN endpoint、二重bufferを合わせたFIFOの予算を計算します（[6.3](#63-fifoの配分)の式）。
収まらなければ`ESP_ERR_INVALID_SIZE`で拒否します。PHYに触る前なので、Hostには何も起きません。`DescriptorsOnly`でも同じ検査をします。

- 同じendpoint番号をalternate settingで使い回すときは、最大のpacketで1回だけ数えます。
- 前もって検査するのは、FIFOに入らない構成は、SET_CONFIGURATIONでendpointを開けないからです。
- isochronousのendpointは、足りなくても気付きにくい壊れ方をします（Hostはdriverを当て、deviceは送ったと報告し、Hostには何も届かない）。AudioのcaptureとVideoでは特に大事です。

---

## 7. HSとFS

### 7.1 最大packet長

| 転送 | FS | HS |
| --- | --- | --- |
| control | 64（このライブラリ） | 64 |
| bulk | 8 / 16 / 32 / 64 | 512 |
| interrupt | 1〜64 | 1〜1024 |
| isochronous | 1〜1023 | 1〜1024 |

endpointを開くとき、coreはこの範囲を検査します。HSのbulkは512以外を拒否します（USB 2.0 §5.8.3）。

### 7.2 HSで変わる値

P4の既定（`controller = Auto`）は、HS controllerを使います。HSでは次が変わります。

| 対象 | FS | HS |
| --- | --- | --- |
| bulk MPS | 64（Vendorは8/16/32も可） | 512 |
| 借用RX（`readDirect`）の容量 | 64の倍数 | 512の倍数 |
| `EndWithShortPacket`でZLPが付く長さ | 64の倍数 | 512の倍数 |
| MSCの`transferBytes` | 64の倍数 | 512の倍数（それ以外は`begin()`が`INVALID_SIZE`） |
| CCIDの`messageBytes`の既定（0=自動） | 271 | 512 |
| HIDのinterrupt MPS | 64まで | 1024まで |
| 非control IN | 4本 | 7本 |

CDC/Vendor/MIDIの`transferBytes`は、MPS未満でも受け付けます。
FSで動いていた設定が、HSでは`begin()`で失敗することがあります。戻り値と`lastError()`を確かめてください。

### 7.3 controllerの選び方とFSへの切替え

| `config.controller` | 動作 |
| --- | --- |
| `Auto`（既定） | HS controllerがあればHS（P4）、なければFS（S2/S3） |
| `FullSpeed` | FS controllerを使う。P4ではFS用のportになる |
| `HighSpeed` | HS controllerを使う。ない機種では`begin()`が`ESP_ERR_NOT_SUPPORTED` |

- HS controllerにFSのHostやhubをつなぐと、FSで動きます。そのため、HS controllerを使うときは、FS用とHS用の両方のdescriptorを`begin()`で作ります。
- FSで動いたときの値（bulk MPS 64など）は、つながってから決まります。借用RXの容量など、MPSに依存する検査は、実際の速度で行います。
- 未知のSoCを、既存のportに割り当てることはしません。ESP32-S31は現在のSDKでは未対応です。

---

## 8. cacheとDMA

### 8.1 DMAの方式

全機種で、DWC2のbuffer DMA modeを使います。CPUがFIFOへ1 packetずつ書くslave modeは使いません（内部DMAのないcontrollerは、初期化を拒否します）。

DMAが触る領域は、すべて内部RAMです。

| 領域 | 確保 |
| --- | --- |
| core領域（EP0のSETUP/data、DWC2 record） | ライブラリ。内部RAM、DMA可能、64 byte境界 |
| classのDMA用buffer、copy送信のstaging | ライブラリ。同上 |
| 借用転送のbuffer | 利用者。DMA可能な内部RAM、先頭64 byte境界 |

### 8.2 P4のcache同期

P4は、CPUがL1 data cache越しに内部RAMへ触ります（cache lineは64 byte）。S2/S3にはこのcacheがなく、同期の処理は空です。
cacheの操作はライブラリが行います。利用者がSoCごとに`esp_cache_msync`を呼ぶ必要はありません。

| 時点 | 処理 | 理由 |
| --- | --- | --- |
| IN DMAの開始前 | 送るbufferをclean（cacheからmemoryへ書き戻す） | DMAが古いdataを送らないため |
| OUT DMAの開始前 | 受信bufferもclean | DMAの途中にdirtyな行が追い出されると、受信dataを上書きするため |
| OUT DMAの完了後 | 受け取った範囲をinvalidate | CPUが古いcacheを読まないため |
| 開始時 | SETUP領域をclean→invalidate | 初期化で残ったdirtyな行を、DMAの前に片付けるため。失敗したら`begin()`が`ESP_FAIL` |

OUTの前のcleanは、P4XのHS借用RXで実際に起きた破損を直したものです。
（65535 byteの受信で途中から後ろが書き込まれず、完了は成功と報告していた。）

利用者が守ること：

- 借用bufferは、完了callbackが返るまでCPUから読み書きしない。P4では、DMA中の書込みが失われたり、受信dataを壊したりします。
- 借用RXの容量は64の倍数にする（ライブラリが検査します）。先頭も64 byte境界なので、bufferが他の変数とcache lineを共有しません。

### 8.3 DMAの分割

1回のDMA設定で運べる量は、controllerのcounterの幅で決まります。
例えばS3のpacket counterは7 bitで、MPS 64なら1回8128 byteまでです。

借用転送は、この制限を利用者に見せません。ライブラリが内部で分割します。

- 途中の区切りはMPSの倍数にする。途中にshort packetやZLPは入らない。
- 完了は、転送全体に対して1回だけ返る。
- 長さの上限は`maxDirectTransfer()`で読める（32 bitのESPでは2147483647 byte）。これは表現できる上限で、確保できる量ではありません。

controlとisochronousの1回の投稿は、これまでどおり16 bitまでです。

### 8.4 ISO OUTの取りこぼし

DWC2のISO OUTは、armするときに「次のframe（HSではmicroframe）が偶数か奇数か」を指定します。
Hostが1 intervalを空けて送ると偶奇が合わず、incomplete ISO OUT割込みになります。

ライブラリはISRの中で、同じDMA bufferのまま偶奇を反転してarmし直します。dataは受け取っていないので、古いdataを渡す心配はありません。
workerを通してarmし直すと、HSの125 µsのmicroframeに間に合わず、次のpacketも失うためです。
この割込みは、ISO OUTを開いたときだけ有効にします。

---

## 9. copy転送と借用転送

CDCとVendorには、2つの経路があります。MIDIはcopy経路（packet API）だけです。

| 経路 | API | bufferの持ち主 | 向いている用途 |
| --- | --- | --- | --- |
| copy | `write()`、`read()`、`available()` | 呼出しが戻ればアプリへ戻る | 普通の送受信 |
| 借用 | `writeDirect()`、`readDirect()` | 完了callbackまでライブラリ | 大きなdata、copyを避けたいとき |

### 9.1 copy経路

- `write()`はringへcopyし、受け付けたbyte数を返します。Hostに届いたことは意味しません。ringが足りなければ一部だけ受け付けます。
- 受信はringに入ります。ringが満ちたら、bulkの受信を止めてHostを待たせます（backpressure）。黙って上書きしません。
- bulkはbyte streamです。1回の`write()`とHostの1回のreadの境界は一致しません。区切りはprotocolで持ってください。

容量や終端（ZLP）の設定は、classごとです。[CDC](classes/cdc.ja.md)、[Vendor](classes/vendor.ja.md)を参照してください。

### 9.2 借用転送の条件

| 条件 | 違反したとき（`EspUsbTransferError`） |
| --- | --- |
| 完了callback（`onDirectTxComplete`/`onDirectRxComplete`）を`begin()`の前に登録している | `BadArgument` |
| 先頭が64 byte境界 | `NotAligned` |
| 全域がDMA可能な内部RAM（PSRAMは不可） | `NotDmaCapable` |
| 長さが1以上、`maxDirectTransfer()`以下 | `BadArgument` |
| RXの容量が64の倍数で、かつ今のMPSの倍数 | `BadArgument` |
| RXは`rxMode = Borrowed`のときだけ | `NotSupported` |
| Hostが構成済みで、endpointが開いている | `NotMounted` |
| 同じ方向に進行中の転送がない（TXは、copy送信の残りもない） | `Busy` |
| controllerが受け付けた | `TransferFailed` |

- `writeDirect`/`readDirect`は`EspUsbTransferSubmission`を返します。`if (submission)`で受理を確かめます。
- 拒否されたときは、bufferを借りず、callbackも来ません。
- 受理されたときは、0でない`id`が付きます。`id`は`begin`/`end`をまたいでも増え続けます。
- 1つのFunctionで同時に進められる借用転送は、方向ごとに1つです。`Busy`は異常ではなく、普通のbackpressureです。
- TXの長さには、64 byteやMPSの倍数という条件はありません。
- `writeDirect`の終端は、`Continue`（ZLPを付けない）か`EndWithShortPacket`（長さがMPSの倍数ならZLPまで送ってから完了）です。

### 9.3 返却（完了callback）

完了callbackはUSB workerで、受理した転送ごとにちょうど1回呼ばれます。
`EspUsbTransferResult`には、`buffer`、`requestedBytes`、`actualBytes`、`actualBytesKnown`、`status`、`id`が入ります。

| `status` | 意味 | `actualBytes` |
| --- | --- | --- |
| `Success` | 完了した。Hostのapplicationが処理したことまでは意味しない | 有効 |
| `BusReset` | bus reset、構成の解除・再選択、queueあふれの回復で破棄した | 不明（`actualBytesKnown=false`） |
| `Stopped` | `end()`でDMAを止めてから返した | 不明 |
| `TransferError` | 下位の転送エラー、またはENDPOINT_HALT | 分かる範囲だけ |
| `Cancelled` | 取消し、SET_INTERFACE、CLEAR_FEATURE(HALT)で返した | 不明 |

- RXは、bufferが満ちるか、short packet / ZLPを受けると完了します。次の受信には、`readDirect()`をもう一度呼びます。
  投稿していない間はOUTを受け付けず、Hostを待たせます。
- callbackの中で、準備済みの次のbufferを投稿できます。次のbufferをcallbackの中で作ると、その間endpointが遊びます。
  片方を送っている間にもう片方を作り、callbackは投稿だけにしてください。

### 9.4 取消し

```cpp
auto s = vendor.writeDirect(buffer, length);
// …
vendor.cancelDirectTx(s.id);   // id=0なら今の借用が対象
// bufferは、onDirectTxCompleteでCancelledなどとして返るまで触らない
```

- `cancelDirectTx(id)`/`cancelDirectRx(id)`の`ESP_OK`は、「取消しを受け付けた」という意味です。bufferはまだ戻っていません。
- ライブラリはDMAを止め、queueにfenceを置いて古い完了eventを片付けてから、完了callbackでbufferを返します。DMAが止まる前には返しません。
- 完了と取消しが競合しても、結果は1つだけです。`Success`で返ることもあります。
- 対象がない、または`id`が違えば`ESP_ERR_INVALID_STATE`です。古い`id`で新しい転送を取り消すことはありません。
- 同じ転送への2回目の取消しは`ESP_OK`です。resetや`end()`と重なると、`BusReset`/`Stopped`で返ることがあります。
- 待ちを打ち切っても（timeout）、bufferの返却にはなりません。必ず完了callbackを待ってください。
- 取消しはHostへSTALLせず、DATA PIDも変えません。

### 9.5 reset、標準要求、endとの関係

| 出来事 | 借用中の転送 | copyのring |
| --- | --- | --- |
| bus reset | `BusReset` | 捨てる |
| SET_CONFIGURATION（解除・再選択） | `BusReset` | 捨てる |
| SET_INTERFACE（alt 0、data interface） | `Cancelled`。両方向をDATA0へ | 対象方向を捨てる |
| SET_FEATURE(ENDPOINT_HALT) | 対象endpointだけ`TransferError`。HALTのまま | 対象方向を捨てる |
| CLEAR_FEATURE(ENDPOINT_HALT) | 対象endpointだけ`Cancelled`。DATA0へ | 対象方向を捨てる |
| queueあふれの回復 | `BusReset` | 捨てる |
| `end()` | `Stopped` | 解放する |

- bus resetの始まり（ISR）で、coreは構成を無効にし、新しい投稿とDMAの登録を拒否します。workerがreset処理を終えるまで続きます。
- 構成を作り直すたびに、「構成の世代」を進めます。queueに残った古い世代の完了eventは捨てます。
  同じendpoint番号を再利用しても、古い完了が新しい借用を返すことはありません。
- `write()`で受け付けたが未送信のdataは、上の出来事で失われることがあります。確実に届ける必要があるなら、アプリ側で確認と再送をしてください。
- 上の表はCDC/Vendorの動作です。他のclassは各class文書を参照してください。

### 9.6 ZLPと転送の終わり

bulkの転送は、MPS未満のpacket（short packet）で終わります（USB 2.0 §5.8.3）。
長さがMPSのちょうど倍数だと、受け手は「まだ続く」と解釈します。終わりを示すにはZLPが要ります。

| 経路 | 終端の指定 |
| --- | --- |
| 借用TX | 転送ごとに`EspUsbTransferTermination`（`Continue` / `EndWithShortPacket`） |
| copy TX | `streamTermination`（Vendorの既定は`Continue`、CDCの既定は`EndWithShortPacket`） |

「特定の長さのときだけHostのreadが返らない」「ときどき空のreadが返る」は、ここが原因であることが多いです。
Host側は、ZLPが来る場合と来ない場合の両方を前提にしてください。isochronousには、bulkのZLPの規則を使いません。

---

## 10. PSRAMの使い方

PSRAMは、CPUが読み書きする保存先として使えます。USBのDMAが触る領域は、内部RAMのままです。

| 領域 | PSRAM |
| --- | --- |
| CDC/Vendorのring（`rxBuffer`/`txBuffer`に利用者が渡す） | 使える。境界の条件もない |
| 自動確保のring | `malloc`で確保する。置き場所はsdkconfigのmalloc設定による |
| MSCの保存先（RAM disk、callbackの先） | 使える |
| classのDMA用buffer、copy送信のstaging | 使わない（ライブラリが内部RAMに確保） |
| 借用転送のbuffer | 使えない（`NotDmaCapable`） |
| MSCの`transferBuffer`など、DMAへ渡す利用者buffer | 使えない（`configure()`が`ESP_ERR_INVALID_ARG`） |

- 利用者が渡したringは、ライブラリが解放しません。`begin()`から`end()`まで保持してください。
- PSRAMのdataを借用転送で送りたいときは、アプリが内部RAMの借用bufferへcopyしてから投稿します。ライブラリは黙ってbounce copyしません。

確認した範囲：P4のFS loopbackとP4/P4XのHS直結で、Vendorのring（各8 KiB）とMSCの保存先をPSRAMに置いた試験に成功しています。

---

## 11. suspendとremote wakeup

### 11.1 suspend

busが3 ms以上idleになると、deviceは休止（suspend）に入ります（USB 2.0 §7.1.7.6）。

- `suspended()`がtrueになります。構成は保たれ、`ready()`もtrueのままです。
- 休止はresetではありません。借用bufferは自動では返りません。完了、取消し、reset、`end()`のどれかで返ります。
- 復帰（resume）は、controllerのresume割込みか、SOFの再開で検出します。
- 最初のSETUPを受ける前の休止は、休止として扱いません。
- 休止中の消費電流（§7.2.3）を抑えるのは、boardとアプリの仕事です。ライブラリはSoCのsleepや周辺回路を制御しません。

### 11.2 remote wakeup

休止中のHostを、Device側から起こす機能です。次の3つを分けて扱います。

| 段階 | 誰が決めるか | API |
| --- | --- | --- |
| 能力の広告 | アプリ（`begin()`の前） | `config.remoteWakeupSupported`（既定false）。configurationのbmAttributes bit5になる |
| 使用の許可 | Host（SET_FEATURE / CLEAR_FEATURE） | `remoteWakeupEnabled()`で照会 |
| 起こす要求 | アプリ | `remoteWakeup()` |

```cpp
EspUsbDeviceConfig config;
config.remoteWakeupSupported = true;
device.begin(config);

// loop()などのtaskで、外部のeventがあったとき
if (device.suspended() && device.remoteWakeupEnabled()) {
  esp_err_t result = device.remoteWakeup();
  // ESP_OKは「要求をqueueへ受け付けた」という意味。復帰はsuspended()がfalseになることで確かめる
}
```

| `remoteWakeup()`の戻り値 | 条件 |
| --- | --- |
| `ESP_OK` | 要求を受け付けた。復帰の完了ではない |
| `ESP_ERR_INVALID_STATE` | 開始前・停止中、`DescriptorsOnly`、未構成、休止していない、Hostが許可していない、ISRから呼んだ、要求が保留中 |
| `ESP_ERR_TIMEOUT` | event queueに空きがない |
| `ESP_ERR_NOT_SUPPORTED` | USB OTGのない機種 |

- Hostの許可は、bus resetと`end()`で消えます（§9.4.5）。アプリが自分で許可を立てるAPIはありません。
- 受け付けた後にresume、CLEAR_FEATURE、reset、再構成、停止が起きたら、その要求は実行しません。
- 規格は、busが5 ms以上idleになってからresumeを要求するよう定めています（§7.1.7.7）。workerは5 ms＋1 tick待ってから状態を確かめ直し、信号を出します。
- resume信号の長さは1〜15 msです（§7.1.7.7）。ライブラリはcontrollerのlockの中で2 ms待ちます（`esp_rom_delay_us`）。
  `vTaskDelay(1 ms)`は、tickの途中で呼ぶと1 ms未満になり得るためです。代償として、その間、実行しているcoreの割込みが約2 ms遅れます。
- 使わなければ、timer、task、待ちは増えません。

---

## 12. 独自のFunctionを足す

### 12.1 まず既存のFunctionの上に載せる

新しい機能の多くは、既存のFunctionの上に自分のprotocolを載せれば作れます。

| やりたいこと | 使うもの |
| --- | --- |
| driverなしで独自dataを流す | `EspUsbDeviceHidVendor`（固定長report）、`EspUsbDeviceHidCustom`（自分のreport descriptor） |
| 帯域の要る独自protocol | `EspUsbDeviceVendor`（bulk IN/OUT＋vendor control、WinUSB/WebUSB） |
| serialとして見せる | `EspUsbDeviceCdcSerial` |
| control要求の観測 | `device.onAnyControlRequest()` |

### 12.2 EspUsbDeviceClassの公開範囲

全Functionの基底`EspUsbDeviceClass`は公開されています。ただし公開されているのは、**descriptorを書く部分**までです。

| 公開されているもの | 内容 |
| --- | --- |
| `configurationDescriptor(dst, itf, ep, packet)` | 必須。割り当てられた番号でdescriptorを書き、長さを返す |
| `configurationDescriptorForSpeed(dst, capacity, itf, ep, highSpeed)` | 速度で内容が変わる場合 |
| `interfaceCount()` / `endpointCount()` | 必須。使うinterface数とendpoint番号の数 |
| `functionName()`、`auxiliaryStringCount()`/`auxiliaryString()` | 文字列 |
| `descriptorConfigurationError()` | 設定の誤りを`begin()`へ返す |
| `isHid()`など種類の判定 | 既定はすべてfalse。`isHid()`がtrueなのはHID Function（`EspUsbHidFunction`）だけ |

writerの契約：

- `dst == nullptr`のときは、長さだけを返し、何も書かない。
- 非nullのときは、`capacity`を超えて書かない。
- 構成を変えるまで、数えた長さと書いた長さを一致させる。一致しなければ`begin()`が`ESP_ERR_INVALID_SIZE`で拒否します。
- 渡された番号をそのまま使う。自分で番号を決めない。

```cpp
class MyFunction : public EspUsbDeviceClass {
public:
  explicit MyFunction(EspUsbDevice &device) : EspUsbDeviceClass(device) {}
  uint8_t interfaceCount() const override { return 1; }
  uint8_t endpointCount() const override { return 0; }
  uint16_t configurationDescriptor(uint8_t *dst, uint8_t itf, uint8_t, uint16_t) override {
    const uint8_t bytes[] = {9, 4, itf, 0, 0, 0xff, 0, 0, 0};
    if (dst) memcpy(dst, bytes, sizeof(bytes));
    return sizeof(bytes);
  }
};
```

**制限：** endpointの開閉、転送、class要求への応答を行う「class driver」の登録は内部API（`src/internal/`）で、v3では公開していません。
coreはSET_CONFIGURATIONのとき、すべてのinterfaceをどれかのclass driverに受け持たせます。受け持つdriverのないinterfaceがあると、SET_CONFIGURATIONは失敗します。
そのため、`EspUsbDeviceClass`を直接継承しただけのFunctionは、Hostとの通信には使えません。
用途は、`DescriptorsOnly`でのdescriptorの検証などに限られます。新しいUSB classを実装するには、ライブラリ本体の変更が要ります。

### 12.3 本体にclassを足す場合の手順

ライブラリ本体にclassを足すときは、次の順で進めると、原因を切り分けやすくなります。

1. `DescriptorsOnly`でdescriptorだけを作り、byte列とendpoint予算を確かめる。
2. 実機で列挙させ、Host側の`lsusb -v`などと照らし合わせる。
3. data経路を1方向ずつ確かめる。
4. reset、構成の再選択、HALT、取消し、`end()`の各経路で、bufferが1回だけ返ることを確かめる。
5. `validation/`の標準試験（`--combo all`）を通す。

内部の設計は[docs/development/architecture.ja.md](development/architecture.ja.md)を参照してください。

---

## 13. 計測とデバッグ

### 13.1 何で確かめるか

| 知りたいこと | 手段 |
| --- | --- |
| `begin()`の失敗理由 | `device.lastError()` / `lastErrorName()` |
| 自分が出しているdescriptor、endpoint予算 | [`EspUsbDeviceDescriptorDump`](../examples/Info/EspUsbDeviceDescriptorDump/)（Host不要） |
| 列挙までの段階 | [`EspUsbDeviceBringUpCheck`](../examples/Info/EspUsbDeviceBringUpCheck/) |
| Hostが受け取った内容 | `lsusb -v`（Linux）、USB Device Tree Viewer（Windows） |
| Hostが送ったcontrol要求 | `device.onAnyControlRequest()` |
| bulk INの二重buffer | `device.bulkInDoubleBuffered()` |
| Hostが決めた速度（FS／HS） | `device.speed()` |
| 2台構成の自動試験 | `validation/`（`pytest --combo all`） |

### 13.2 切り分けの原則

1. **`begin()`が成功したかを先に見る。** 失敗しているなら、Hostは関係ありません。
2. **Hostにつながずにdescriptorを読む。** 意図どおりでなければ、その先を調べても無駄です。
3. **自分の出力とHostの受け取りを比べる。** 一致していれば、問題はdescriptorの上か下に絞れます。
4. **Functionを1つずつ外す。** 複合deviceの問題が単機能で消えるなら、原因は予算か並び順です。
5. **予期しない再列挙は、queueのあふれを疑う。** callbackが重くないか、workerのpriorityを下げていないか（[2.5](#25-event-queueとあふれ)）。
6. **一度に複数を変えない。** class構成、MPS、buffer容量は1つずつ。

### 13.3 速度について

帯域は、Host側の作り方に大きく左右されます。

- Hostが同時に投げるreadの数（in-flightのURB）を増やす。1本ずつだと、URBの間にendpointが遊びます。
- Hostの完了callbackでは、bufferの再投入だけをする。解析などの重い処理をcallbackの中で行うと、streamに穴が空きます。
- device側では、bulk INの二重buffer（[6.4](#64-bulk-inの二重buffer)）、大きな転送長、借用転送のcallbackで投稿だけをすることが効きます。

v3での帯域の基準値は、まだ測っていません。

---

## 14. v2との違い

| v2 | v3 |
| --- | --- |
| TinyUSBのdevice stackとclass driverを同梱してbuild | 使わない。本体のcoreとFunctionが処理し、DWC2 driverだけTinyUSB由来の保守fork |
| `tusb_config.h`と`build_opt.h`の`CFG_TUD_*`で容量を調整 | 各Functionの`configure()`で、停止中に指定。旧flagはcompile error |
| `config.startTinyUsb = false` | `config.startMode = EspUsbDeviceStartMode::DescriptorsOnly` |
| `device.task()` | 削除。USB workerが処理する |
| workerのstack/priorityは固定 | `taskStackBytes`/`taskPriority`/`taskCoreId`/`eventQueueDepth` |
| Function（class）は6個まで | 251個まで（endpoint/FIFOの予算は別に検査） |
| configuration descriptorは704 byte、複合HID reportは256 byteまで | 実際の長さで確保。上限はUSBの16 bit |
| 複合HIDのreport IDは型ごとに固定（Keyboard=1など） | 構成ごとに一意に割り当てる。`compositeReportId()`で読む |
| 文字列はASCIIを16 bitへ広げ、63文字で切り詰め | UTF-8をUTF-16へ変換。126 code unitまで。超過や不正はエラー |
| WebUSB URLは、prefixなしをhttpsとみなし、切り詰める | prefixなしはscheme 255で全文を保存。長すぎるとエラー |
| `writeDirect`はbuild flagで有効にし、65535 byteまで | 常に使える。投稿結果は`EspUsbTransferSubmission`。長さに16 bitの上限なし。`readDirect`と取消しを追加 |
| `end()`は戻り値なし | `esp_err_t`。成功後は、DMAもcallbackもbufferに触れない。workerの停止待ちは`stopTimeoutMs`まで（`ESP_ERR_TIMEOUT`） |
| `usbd_app_driver_get_cb`でclass driverを追加 | 内部API。公開していない（[12.2](#122-espusbdeviceclassの公開範囲)） |
| remote wakeupなし | `remoteWakeupSupported`、`remoteWakeupEnabled()`、`remoteWakeup()` |
| 休止の照会なし | `suspended()` |
| 速度の照会なし | `speed()` |
| VBUS検出は`CFG_TUD_VBUS_DETECT_HW` | `config.vbusSensing` |

移行の手順は[v2からの移行](migrating-from-v2.ja.md)を参照してください。

---

## 15. 確認した範囲

| 環境 | 状況 |
| --- | --- |
| ESP32-S3 二台のFS peer | 主な試験環境。全classと共通の仕組み |
| ESP32-P4 一台のHS↔FS loopback | 両controllerで、各classの基本通信 |
| 旧P4（v1.x）とP4X（v3.x）のHS直結 | 両方向 |
| P4XとLinux PCのHS直結 | DFU/MSCのwire |
| ESP32-S2 | compileのみ。実機は未確認 |
| ESP32-S31 | 未対応 |
| Windows / macOS | v3では未確認 |

標準の試験は、`validation/`の`pytest --combo all`です（6組、200件超）。基準はArduino-ESP32 3.3.12です。

この文書の内容のうち、次は確かめていません。

- remote wakeupの、S2/P4実機とPCのOSでの動作（S3–S3だけで確認）。
- `vbusSensing = true`で、実際にcableを抜いたときのVBUS検出。
- HSの電気的な適合性とcompliance test（`SET_FEATURE(TEST_MODE)`の各test信号を測定器で確かめていない）。
- v3での帯域の基準値。

---

## 関連文書

- [USB Device開発ガイド（入門編）](usb-device-guide.ja.md)
- [example workflow](example-workflow.ja.md)
- [troubleshooting](troubleshooting.ja.md)
- [USB経由のfirmware更新](ota-over-usb.ja.md)
- [v2からの移行](migrating-from-v2.ja.md)
- [Arduino-ESP32のUSBからの移行](migrating-from-arduino-esp32-usb.ja.md)
- class別：[HID](classes/hid.ja.md)、[CDC](classes/cdc.ja.md)、[Vendor](classes/vendor.ja.md)、[MIDI](classes/midi.ja.md)、[MSC](classes/msc.ja.md)、[Audio](classes/audio.ja.md)、[Video](classes/video.ja.md)、[NCM](classes/ncm.ja.md)、[CCID](classes/ccid.ja.md)、[DFU](classes/dfu.ja.md)
- [USB backendの由来と保守](../third_party/tinyusb/PROVENANCE.ja.md)
- [内部設計](development/architecture.ja.md)、[試験](development/testing.ja.md)
- 規格：[USB 2.0 specification](https://www.usb.org/document-library/usb-20-specification)、[WebUSB](https://wicg.github.io/webusb/)、[Microsoft OS 2.0 descriptors](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/microsoft-os-2-0-descriptors-specification)
- Host側：[EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost)
