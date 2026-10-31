# v3 の設計と理由

保守者向けの文書です。EspUsbDevice v3 の内部構造と、その形にした理由を書きます。
使い方は [利用ガイド](../usb-device-guide.ja.md) と各 class の文書（`docs/classes/*.ja.md`）を見てください。

- 基準：Arduino-ESP32 3.3.12。
- 根拠は実装（`src/EspUsbDevice.h`、`src/*.cpp`、`src/internal/`）です。この文書と実装が食い違うときは実装が正です。
- USB の挙動は USB 2.0 と各 class 仕様に合わせます。規格の該当箇所を本文に書きます。

## 基本方針

| 方針 | 中身 |
| --- | --- |
| 明示的な構成 | Device に Function を付け、設定してから `begin()` する。USB を自動で起動しない |
| 使う機能だけに費用 | 使わない class のコード・RAM・task を持ち込まない |
| 容量は実行時に決める | 容量・個数・転送方式は `configure()` と `begin()` で決める。`build_opt.h` は使わない |
| 所有権を一つに | どの資源も持ち主は一つ。借りた buffer は一度だけ返す |
| 黙って変えない | 容量を丸めたり、切り詰めたり、通知を捨てたりしない。合わない設定は error で返す |
| 規格を優先 | Host の不具合を Device 側の非準拠な回避で隠さない |

Arduino-ESP32 標準の USB Device stack（`USB.h` 系）とは同時に使えません。
TinyUSB の class・共通 stack は使わず、DWC2 driver だけを由来コードの保守 fork として本体に持ちます（[DWC2 driver](#dwc2-driver)）。

## 層と依存の向き

```text
利用者のスケッチ
  ↓
EspUsbDevice（Device）と Function（HID, CDC, Vendor, MIDI, MSC, Audio, Video, NCM, CCID, DFU）
  ↓                         ↑ 任意の helper（SD, FAT, firmware, IP）は必要な Function だけを参照
descriptor の生成 / endpoint の割当て / 転送の契約
  ↓
core（EP0、標準要求、event queue、世代）       src/internal/EspUsbDeviceCore.*
  ↓
runtime（PHY の取得、controller 選択、USB worker） src/internal/EspUsbRuntime.*
  ↓
controller 境界                                   src/internal/EspUsbController.h
  ↓
DWC2 driver と platform（register, FIFO, DMA, IRQ, cache）
                                                  src/internal/EspUsbDwc2*.{c,cpp,h}
  ↓
ESP-IDF（PHY、FreeRTOS、heap、cache）
```

| 層 | 主なファイル | 役割 |
| --- | --- | --- |
| Device / Function | `src/EspUsbDevice.h`、`src/EspUsbDevice*.cpp`、`src/EspUsbAudio.cpp`、`src/EspUsbVideo.cpp`、`src/EspUsbHidFunction.cpp` | 利用者 API、設定の検査、class の処理 |
| 共通 bulk | `src/EspUsbDeviceBulkStream.cpp` | CDC / Vendor / MIDI が使う bulk 転送、ring、借用転送、取消し |
| core | `src/internal/EspUsbDeviceCore.*` | EP0、標準要求、event、世代 |
| runtime | `src/internal/EspUsbRuntime.*` | PHY、controller の選択、worker task |
| DWC2 | `src/internal/EspUsbDwc2Device.c`、`EspUsbDwc2Phy.c`、`EspUsbDwc2Registers.h`、`EspUsbDwc2Hardware.h` | register、FIFO、DMA |
| platform | `src/internal/EspUsbDwc2Platform.*` | SoC ごとの port 表、IRQ、cache 同期 |
| wire 型 | `src/internal/EspUsbWire.h` | SETUP（8 byte）、interface（9 byte）、endpoint（7 byte）の layout |

依存の規則：

- Device は登録された Function だけを見ます。全 class を並べた switch、全 class への extern 参照、全 class 分の最大配列は持ちません。
- 各 class と helper は別の translation unit です。共通 header から SD、IP、OTA の実装 header を include しません。
- `library.properties` の `dot_a_linkage=true` で library を archive としてリンクします。使わない `.o` は取り込まれません。
  理由：直接リンクでは、未使用の `EspUsbDeviceNetif.cpp.o` が `IPAddress` の参照を通じて Arduino core の network 関連を引き込んでいました。archive にすると include だけの増分は 0 になります。
- 内部 header は利用者 API ではありません。

## 所有権と寿命

- Device は Function より長く生きます。宣言は Device → Function の順、終了は `end()` → Function の破棄 → Device の破棄の順です。
- Device と Function は copy も move もできません。
- Function は constructor で Device の侵入型 list に自分を登録します。登録に heap を使わないので、constructor の確保失敗で Function が黙って抜け落ちることはありません。
- `begin()` 中と開始後の登録・解除は拒否します。未登録の Function が残ったままの次の `begin()` も拒否します。
- Function の `begin()`、`afterDeviceStarted()`、`prepareToStop()` は protected です。呼ぶのは `EspUsbDevice::begin()` / `end()` だけです。sketch が `console.begin()` などを書くと compile error になります。
  理由：Function を単独で開始すると、Device の確保・descriptor・worker とずれた状態になり、次の `device.begin()` が開始できなくなるためです。
- Function 数の上限は 251 です。理由：文字列 index の 5〜255 を Function 名に割り当てるためです。
- 稼働中の Function の破棄は Device 全体を止めます。DMA が触っている領域を残したまま object が消えるのを防ぐためです。

寿命と copy：

| 対象 | 扱い |
| --- | --- |
| 数値の設定 | 値として保持する |
| 文字列（manufacturer など、Function 名） | `begin()` で UTF-16 に変換して copy する。成功後は元の配列を解放してよい |
| 大きな利用者 buffer（外部 ring など） | 借用する。class の ring は `end()` の成功まで保持する |
| 借用転送の buffer | その転送の完了通知まで保持する |
| callback の context | 借用する。`end()` の成功まで有効にしておく |

## 資源の確保と解放

### いつ確保するか

| 時期 | 行うこと |
| --- | --- |
| constructor | Device への登録と軽い状態だけ。heap は使わない |
| 停止中の `configure()` | 値の検査と保存だけ。Audio の format 一覧など、設定の保存に要る分は確保する |
| `begin()` | 検証 → 確保 → descriptor 生成 → runtime 開始 → attach |
| 転送中 | heap を確保しない |
| `end()` | 受付を閉じる → 切断 → DMA の停止と取消し → 完了の回収 → worker の停止 → 解放 |

`begin()` の中身：

1. 登録 list から interface / endpoint の要求を集め、controller の能力表と照らして全体を計画します。
2. descriptor の長さを数え、実数分を確保して書きます（[descriptor の生成](#descriptor-の生成)）。
3. core の状態、interface / endpoint 表、event queue、EP0 の DMA 領域、DWC2 の転送 record を**一つの内部 DMA 領域**にまとめて確保します。
4. 各 Function が ring・staging・状態を確保します。
5. PHY を取り、IRQ を登録し、worker を起動して attach します。

- どこで失敗しても、取得の逆順に戻して停止状態へ戻ります。次の `begin()` は再び実行できます。
- 失敗の理由は `lastError()` で読めます（例：`ESP_ERR_NO_MEM`、`ESP_ERR_INVALID_SIZE`、`ESP_ERR_INVALID_ARG`）。
- 設定の誤りは PHY・IRQ・worker を始める前に返します。endpoint の数が足りない構成は、descriptor を作る段階で `ESP_ERR_INVALID_SIZE` になります。
- descriptor の後、各 Function の確保より前に、構成全体の TX FIFO の予算を検査します。入らなければ `ESP_ERR_INVALID_SIZE` です（[controller の能力表](#controller-の能力表)）。
- Function を一つも登録しない `begin()` は controller を起動しません。
- DWC2 の転送 record は、全 alternate の descriptor に出てくる endpoint の方向の数と、EP0 の 2 方向分だけ作ります。

### 理由

- **容量を実行時に決める**：v2 は容量や転送方式を `CFG_TUD_*` などの build flag で決めていました。値を変えるには `build_opt.h` と clean build が要り、一つの binary で構成を変えられませんでした。v3 では `end()` → `configure()` → `begin()` で、再 build せずに変えられます。
  「実行時設定」は**停止してから設定し直せること**を指し、接続したまま descriptor を書き換えることではありません。
- **`begin()` でまとめて確保する**：転送の途中で heap を取らなければ、確保失敗は `begin()` の一か所で扱えます。
  転送中の確保失敗は、どの転送をどう失敗させるかが決めにくく、buffer の返却漏れの原因になります。
- **一つの領域にまとめる**：確保と解放の点が減り、失敗時の巻き戻しが単純になります。
- **使わない方向は作らない**：送信専用の構成で受信 ring を持つ、といった無駄をなくします（`EspUsbBulkRxMode::Disabled` など）。
- **旧 build flag は compile error**：`src/internal/EspUsbLegacyFlags.h` が `CFG_TUD_*` などの指定を拒否します。0 を指定しても拒否します。黙って無視すると、利用者は設定が効いていると誤解するためです。

### `end()` の契約

- `end()` が成功して戻った後は、callback も DMA も利用者の buffer に触れません。
- 停止を確かめられない buffer は解放しません。timeout だけで停止したことにはしません。
- worker の終了は `EspUsbDeviceConfig::stopTimeoutMs`（既定 5000 ms、0 は無期限）まで待ちます。期限までに worker が実行中の callback を終えなければ、`end()` は `ESP_ERR_TIMEOUT` を返します。
  停止の要求は残り、資源も保持します。callback が戻った後にもう一度 `end()` を呼ぶと停止が完了します。
  理由：利用者の callback が戻らないとき、`end()` を呼んだ task まで止まり続けるのを防ぐためです。
- destructor と Function の `end()` は、停止が終わるまで期限なしで待ちます。Function や Device の memory を worker が使っている間に解放しないためです。
- USB worker の中から `end()` を呼ぶと `ESP_ERR_INVALID_STATE` です。worker が自分の終了を待つことになるためです。

## USB worker と排他

### 実行する場所

| 処理 | 場所 |
| --- | --- |
| 割込みの受付、event の生成、ISO OUT の偶奇の再 arm | ISR |
| EP0、標準要求、class の処理、完了通知、reset / end の通知 | USB worker（FreeRTOS task） |
| SOF の class callback | ISR |
| 設定、`begin()` / `end()`、copy write | 利用者の task |

worker の設定は `EspUsbDeviceConfig` で指定します。

| 項目 | 既定 | 意味 |
| --- | --- | --- |
| `taskStackBytes` | 4096 | 利用者 callback の分も含む stack |
| `taskPriority` | -1 | -1 は `configMAX_PRIORITIES - 1` |
| `taskCoreId` | -1 | -1 は core を固定しない |
| `eventQueueDepth` | 16 | ISR から worker へ渡す event の数。0 は不可 |

- ISR の処理は最小にします。class の処理を ISR に入れないのは、利用者 callback と DMA buffer の寿命管理を一つの task に集めるためです。
- `eventQueueDepth` とは別に、hardware event 用の予備を 1 件確保します。
  理由：深さ 1 のとき、標準要求の fence が EP0 status の枠を取り、意図しない再列挙が起きたためです。

### callback の規則

- USB callback は ISR ではありませんが、block、重い計算、`end()`、構成の変更は禁止です。
- callback の中で、同じ endpoint への次の投稿と、他の task への signal はできます。
- 投稿した直後に、その場（inline）で callback を呼ぶことはしません。
  ただし別 core の worker が、投稿元が戻る前に完了を処理することはあります。投稿の前に context を準備してください。
- ライブラリは内部 lock を持ったまま利用者 callback を呼びません。

### 排他

| lock | 守るもの |
| --- | --- |
| core の `hardwareLock` | endpoint 割込みと task 側の DWC2 操作。停止待ちと割込みの同期 |
| core の `endpointLock`、event queue の lock | endpoint 状態と event の出し入れ |
| class ごとの `portMUX`（`bulkLock`、`cdcLock`、`hidLock`、`mscLock`、`ccidLock`、`netLock`、`videoLock`、`audioLock`） | 利用者 task と worker が共有する class の状態 |
| runtime の `g_jobsLock` | worker への deferred job |

- どれも短い critical section です。lock の中で待ったり、callback を呼んだりしません。
- deferred job や fence の予約は block しません。いっぱいなら worker の job から再試行します。
- 同じ Device の設定と lifecycle の操作（`configure()`、`begin()`、`end()`）は、利用者が一つの task で直列に呼んでください。ライブラリはこれらを互いに排他しません。

### 世代と古い event

USB では、reset や再構成の後も、前の転送の完了 event が queue に残ることがあります。古い event を新しい転送に当てると、まだ DMA 中の buffer を返してしまいます。これを世代で防ぎます。

| 世代 | 進める時点 | 効果 |
| --- | --- | --- |
| bus 世代 | USBRST を受けたとき | 構成・接続・suspend を無効にする。worker が reset を終えるまで新しい job と DMA を受け付けない |
| SETUP の通し番号 | 新しい SETUP を受けたとき | 古い control 要求の data を新しい要求へ混ぜない |
| 構成世代 | class 全体を reset するたび（SET_CONFIGURATION の解除・再選択など） | 古い世代の完了 event は、endpoint の状態を変える前に捨てる |

- 再構成の前に、有効な endpoint の停止を待ち、残った転送完了割込み（XFRC）も消します。
- event queue があふれたときは、成功した完了 event を捨てません。捨てると借用 buffer が戻らなくなるためです。
  代わりに DMA を止め、class を reset して buffer を返し、古い event を回収してから、切断・再接続します。通信は中断しますが、所有権は崩れません。

## DWC2 driver

### 保守方針

- DWC2 の hardware 処理は、TinyUSB（MIT）の DWC2 driver と ST の register 定義（BSD-3-Clause）に由来する**保守 fork** です。clean-room で書き直したものではありません。
- 保守する file は `src/internal/EspUsbDwc2Device.c`、`EspUsbDwc2Phy.c`、`EspUsbDwc2Hardware.h`、`EspUsbDwc2Registers.h` と、ESP port 由来の `EspUsbDwc2Platform.*` です。
- 由来の commit と保守対象は `third_party/tinyusb/ORIGIN.json` に記録しています。由来と license は [third_party/tinyusb/PROVENANCE.ja.md](../../third_party/tinyusb/PROVENANCE.ja.md) を見てください。
- 自動の vendor 更新はしません。upstream の修正は中身を読んで、必要なものを手で反映します。
- 派生 source の MIT 著作権表示、`third_party/tinyusb/LICENSE`、`LICENSE-ST` を残します。依存を撤去しても出所の表示は消しません。
- `tools/verify_usb_backend.py` が、保守対象 file があること、旧 vendor の path・設定 header・`tusb*.h` の include がないことを offline で確かめます。

理由：

- runtime の資源管理（begin で確保した転送 record、lock、取消し、世代）と hardware 処理を、同じ境界で変更する必要があります。vendor 更新のたびに patch を当て直す方式では、これを保てません。
- 代わりに、upstream の hardware 修正を追いかけて選ぶ作業は自分たちが担います。

### controller の能力表

`src/internal/EspUsbDwc2Platform.cpp` の `espusb_dwc2_ports` が唯一の能力表です。descriptor の検査、bulk IN の二重 buffer、TX FIFO の予算は、すべてこの表を参照します。

| SoC / port | 速度 | endpoint 番号 | 非 control IN endpoint | FIFO（32 bit word） |
| --- | --- | --- | --- | --- |
| ESP32-S2 / S3 | FS | 0〜6 | 4 本 | 256 |
| ESP32-P4 FS port | FS | 0〜6 | 4 本 | 256 |
| ESP32-P4 HS port | HS（480 Mbps） | 0〜15 | 7 本 | 1024 |

- `EspUsbController::Auto` は HS port があれば HS を選びます。`FullSpeed` / `HighSpeed` を明示して、その port がなければ `ESP_ERR_NOT_SUPPORTED` です。
- 未知の SoC を既存の port へ割り当てません。S31 は未対応です。
- IN endpoint 番号と TX FIFO 番号は別です。TX FIFO は、使う IN endpoint の番号順に 1 から詰めます（EP0 は FIFO 0）。
  理由：DWC2 の FIFO の数は endpoint 番号の範囲より少ないことがあり、番号に空きがある構成（Audio と CDC で IN 1/2/4/5 など）では、番号 = FIFO 番号の対応が成り立たないためです。
- bulk IN の二重 buffer（`bulkInBuffering`）は、`Auto` なら全 bulk IN を 2 packet にできるときだけ行います。一部だけにすると、Function の登録順で速度が変わるためです。
- `begin()` は、構成全体の IN endpoint が FIFO に入るかを、driver の割当てと同じ計算で検査します。数えるのは EP0 IN、各 IN endpoint 番号の最大 packet（alternate setting の間では最大の一つ）、二重 buffer の分、RX FIFO（最大の OUT packet から決まる）です。入らなければ `ESP_ERR_INVALID_SIZE` です。
  理由：入らない endpoint は SET_CONFIGURATION で開けず、列挙した後に動かない device になります。isochronous IN は FIFO を置けなくても完了を報告し、Host には何も届きません。
  例：P4 の HS port では、既定の Vendor（bulk IN 512 byte）は 5 個まで入り、6 個目で `begin()` が失敗します（`validation/peer/fifo_budget`）。

### IRQ と cache

- IRQ は `begin()` で一度だけ登録し、`end()` で解放します。稼働中の停止・再開は mask / unmask で行います。
- P4 では DMA を始める前に CPU cache を同期します。S2 / S3 は cache がないので何もしません。

| 方向 | DMA 開始前の処理 | 理由 |
| --- | --- | --- |
| IN | 送る領域を clean（memory へ書き戻す） | DMA が古い memory の内容を送らないため |
| OUT | 受信領域の dirty な行も clean | 下記 |
| 開始時の SETUP 領域 | clean してから invalidate | 遅れた CPU の書戻しが、受信した SETUP を壊さないため |

**OUT DMA の前に clean する理由**：受信領域に CPU が書いた dirty な cache 行が残っていると、DMA の途中でその行が追い出され、受信済みの data の上に書き戻されることがあります。
P4X の HS 借用受信で、65535 byte のうち 9216 byte 以降が壊れ、それでも完了は「成功・実長 65535」と報告される現象として現れました。
受信の前に clean すれば、DMA 中に書き戻される dirty 行がなくなります。完了後の invalidate は従来どおり行います。

- 借用 buffer は先頭を 64 byte 境界にし、受信の容量は MPS と 64 の倍数にします。cache 行を隣の変数と共有すると、上の同期で隣の値を壊すためです。

### ISO OUT の偶奇

DWC2 の ISO OUT は、arm するときに「次の (micro)frame が偶数か奇数か」を指定します。Host が 1 interval 空けて送ると偶奇が合わず、incomplete ISO OUT 割込みになります。

- ISR の中で、同じ DMA buffer のまま偶奇を反転して arm し直します。
- OUT なので古い data を送る心配はありません。回数の上限も設けません。
- incomplete OUT 割込みは、ISO OUT endpoint を開いたときだけ有効にします。EP0、bulk、interrupt OUT は対象外です。

理由：worker で arm し直す方式では、HS の 125 µs の microframe に間に合わず、次の packet も失います。Host が間を空けると 1 packet おきに欠けました。ISR の中なら間に合います。

### その他の hardware 規則

- 周期 endpoint の `bInterval` は、FIFO の確保や endpoint の変更より前に検査し、不正な値を拒否します（USB 2.0 §9.6.6）。
- `vbusSensing`（既定 false）が false なら B-session valid を強制します。検出回路のない board 向けです。true は board に VBUS 検出の配線がある場合だけ使います。

## EP0 と標準要求

- EP0 は SETUP / DATA / STATUS を区別して扱い、受信した実 byte 数を class へ渡します（Vendor control の `actualBytes`）。
- 標準要求は、方向・値・index・長さを検査します（USB 2.0 §9.4）。広告していない remote wakeup や、EP0・ISO endpoint への HALT 設定は拒否します。
- SET_FEATURE / CLEAR_FEATURE(DEVICE_REMOTE_WAKEUP) は、Address 状態と Configured 状態で受け付けます（USB 2.0 §9.4.1、§9.4.9）。
- SET_FEATURE(TEST_MODE) は HS で接続しているときだけ受け付けます。test selector は wIndex の上位 byte の 1〜4（Test_J、Test_K、Test_SE0_NAK、Test_Packet）で、STATUS 段階の後に test mode へ入ります（USB 2.0 §7.1.20、§9.4.9）。抜けるには電源を入れ直します。
- vendor 要求のうち、ライブラリが自分で答えるのは二つだけです。WebUSB の GET_URL（bmRequestType 0xC0、bRequest 0x01、wIndex 2。`webusbEnabled` のとき）と、MS OS 2.0 descriptor set（bmRequestType 0xC0、bRequest 0x02、wValue 0、wIndex 7。set があるとき）です。
  同じ bRequest でもほかの要求は、`EspUsbDeviceVendor::onControlRequest()` などの class へ渡します。
- `device.onAnyControlRequest()` は control 要求の観測だけを行い、応答は変えません。worker が読むので、設定できるのは停止中だけです。稼働中は `ESP_ERR_INVALID_STATE` を返します。
- 未対応の configuration 値への要求は拒否しますが、動いている構成は解除しません。
- SET_CONFIGURATION で解除または同じ値を再選択したら、DMA を止め、借用 buffer を返し、class を初期化し直します。
- SET_INTERFACE、ENDPOINT_HALT の SET / CLEAR は、対象の interface・方向だけを回復させます。関係のない Function や反対方向の転送は続けます。
  CLEAR_FEATURE(ENDPOINT_HALT) は data toggle を DATA0 に戻します（USB 2.0 §9.4.5）。
- `sendControlResponse(..., 0)` は、IN で要求長が 1 以上なら長さ 0 の DATA 段階（ZLP）を送り、要求長 0 なら STATUS だけを返します。
  理由：「長さ 0 なら常に STATUS」だと、要求長のある IN で Host が DATA を待ち続けるためです。

## descriptor の生成

### 二段で作る

- 各 Function の writer は、`dst == nullptr` のとき長さだけを返し、非 null のとき書きます。
- Device は全 Function の長さを足し、実数分を確保してから書きます。同じ構成から長さと中身を作るので、両者が食い違いません。
- FS と HS は別々に照会・出力します。HS を使う controller では、HS configuration と other-speed 応答用の領域も作ります。
- 確保する領域：FS / HS configuration、other-speed、複合 HID の report descriptor（HID を複合するときだけ）、MS OS 2.0 set（WinUSB 対象があるときだけ）、BOS（WebUSB か MS OS を使うときだけ）、USB 文字列、WebUSB URL。
- `begin()` で作った領域は、worker を起動する前に公開します。稼働中の GET_DESCRIPTOR では確保も作り直しもしません。
- `wTotalLength` などが 16 bit であることは USB 規格の上限として残ります。

理由：v2 は configuration 704 byte、HID report 256 byte などの固定領域を持っていました。長い複合構成が入らず、小さい構成では無駄でした。

### 割当て

- interface 番号と endpoint 番号は、Function の登録順に割り当てます。
- IN と OUT は同じ endpoint 番号を共有できます。Audio は方向付き endpoint の個数ではなく、`max(IN 数, OUT 数)` の番号幅を予約します。
- 複合した HID の report ID は、構成の中で一意になるよう割り当てます。通常の送信 API は自動で追従します。
- 文字列 index は、1〜3 が manufacturer / product / serial、4 が予約、5 以降が Function 名、その後に Function 固有の補助文字列（NCM の MAC など）です。

### HS 対応 controller での容量

HS に対応した controller を選んだときは、link が FS でも、bulk の容量を HS の MPS 512 で検査・予約します（`maximumBulkPacketSize()`）。

理由：接続速度は列挙の後にしか分かりません。また、HS の descriptor も同じ資源で応答します。開始時に両方の速度で成り立つ資源を確保しておく必要があります。
決まった速度は `device.speed()`（`EspUsbBusSpeed::None` / `Full` / `High`）で読めます。

結果として、HS 対応 controller では次の値が 512 基準になります。

| 値 | 条件 | 違反したとき |
| --- | --- | --- |
| MSC `transferBytes` | bulk MPS の倍数 | `begin()` が `ESP_ERR_INVALID_SIZE` |
| CCID の明示 `messageBytes` | Bulk-OUT MPS 以上 | `begin()` が `ESP_ERR_INVALID_SIZE` |
| 借用受信（`readDirect`）の容量 | MPS と 64 の倍数 | 投稿が `BadArgument` |
| CDC / Vendor / MIDI の `transferBytes` | MPS 未満も可 | — |

### 文字列

- UTF-8 で受け取り、`begin()` で UTF-16 の copy を作ります。1 文字列は 126 UTF-16 code unit までです（USB 2.0 §9.6.7、`bLength` が 1 byte のため）。
- 超えたら、または不正な UTF-8 なら、`begin()` が `ESP_ERR_INVALID_SIZE` などで失敗します。切り詰めません。
- WebUSB の URL は scheme の推測をせず、prefix のない URL は scheme 255 で全文を保持します。

## 転送の共通契約

### 三つの経路

| 経路 | 戻り値と寿命 |
| --- | --- |
| copy write | copy して受け付けた byte 数。一部だけのこともある。戻った後は元の buffer を再利用してよい。wire 上の完了ではない |
| 借用送受信（`writeDirect` / `readDirect`） | 受理なら非 0 の id。拒否なら error で、そのときは借用も通知もしない |
| 完了通知 | id、buffer、要求長と実長、status。一度だけ |

- 完了の status は `Success`、`BusReset`、`Stopped`、`TransferError`、`Cancelled` です。どの終わり方でも buffer は一度だけ返ります。
- 取消し（`cancelDirectTx` / `cancelDirectRx`）の受理と buffer の返却は別です。DMA が止まるまで buffer は返りません。完了と競合しても結果は一つです。
- 受信は `Stream` / `Callback` / `Borrowed` / `Disabled` を明示して選びます。callback の登録の有無で暗黙に変えません。
- stream 受信の ring が満杯なら bulk の受信を止めて backpressure をかけます。上書きしません。
- 容量の語を分けます：`capacity`（保持できる量）、`transferBytes`（一回に投稿する量）、MPS（wire 上の packet 長）。

### なぜ借用転送を設けたか

- copy write は使いやすい一方、大きな data では copy の費用と ring の容量が問題になります。
- 借用転送は、アプリの buffer をそのまま DMA に渡します。代わりに、buffer の条件（64 byte 境界、DMA 可能な内部 RAM）と、完了まで触らないという負担をアプリが負います。
- 条件に合わない buffer を黙って bounce copy しません。copy しないという契約が崩れるためです。PSRAM の buffer は `NotDmaCapable` で拒否します。
- 投稿結果を値（`EspUsbTransferSubmission`）で返すのは、`lastError()` が別の task や反対方向の投稿で上書きされるためです。
- 長さの 16 bit 上限はありません。controller の一回の DMA 設定に入らない長さは、packet 境界で内部的に分けて arm します。完了は全体で一回です。分割の途中に short packet や ZLP は入れません。

### bulk の終端

- bulk は byte stream です。`write()` 一回と Host の read 一回の境界は一致しません。
- 借用送信は終端を転送ごとに指定します（`EspUsbTransferTermination`）。`EndWithShortPacket` は、長さが MPS の倍数なら ZLP まで送ってから完了を返します（USB 2.0 §5.8.3）。
- copy write の終端は `EspUsbStreamTermination` で、既定は CDC が ZLP 終端、Vendor が `Continue` です。CDC の既定は、Host が大きな read で待っていても少量の data を届けるためです。
- Audio / Video の ISO には bulk の ZLP 規則を使いません。

### PSRAM

- ring、MSC の保存先、Audio の外部 ring など、CPU が読み書きする領域には PSRAM を使えます。
- USB の DMA が触る staging は内部 RAM です。借用転送の buffer は DMA 可能な内部 RAM でなければなりません。

## 各 class の設計判断

### CDC：`Stream` にした理由

- 共通 bulk を protocol に依存しない転送層とし、`Print` を外しました。CDC は Arduino の `Stream`、Vendor は `Print`、MIDI は packet API です。
- 理由：CDC を `Stream` を受け取るコード（parser、`readBytes` など）へ直接渡せるようにするためです。共通層ごと `Stream` にすると、Vendor や MIDI まで不要な timeout と parser の状態を持ちます。
- `write()` / `flush()` は Arduino の `Serial` に合わせ、TX timeout（既定 1000 ms）まで空きを待ちます。`flush()` は DMA と終端の ZLP まで待ちます。`setTxTimeoutMs(0)` で待たずに一部だけ受け付けます。
- worker の中では待たず、ISR では受け付けません。worker が自分の処理する完了を待ち続けるのを防ぐためです。
- 待ちは CDC だけです。Vendor、MIDI、借用投稿は待ちません。待ちのための ring、task、同期 object は追加していません。
- DTR は `write()` の条件にしません。Host が構成を選んでいれば送れます。
- `availableForWrite()` を override して実際の空きを返します。`Print` の既定実装は常に 0 を返し、標準 API だけを使うコードが送信を始められないためです。
- SET_LINE_CODING は DATA 段階の実長が 7 byte でなければ STALL します。短い OUT で前の buffer の末尾を含む値を採用しないためです。

### Vendor

- copy write で受け付けた byte は自動で送出します（`autoFlush` 既定 true）。短い返信が buffer の中で止まらないためです。`flush()` は送出の開始だけで、完了は待ちません。
- FS の bulk の packet 長（constructor の `endpointSize`）は 8 / 16 / 32 / 64 だけです（USB 2.0 §5.8.3）。それ以外は `begin()` が `ESP_ERR_INVALID_SIZE` です。64 より大きい値は 64 になります。HS は常に 512 です。
- Vendor control の callback には `actualBytes` を渡します。Host の `wLength` と実際に受け取った量は同じとは限らないためです。

### MIDI

- USB MIDI 1.0 の 4 byte event packet を単位とする API です。byte stream の `Print` / `Stream` は継承しません。
- 予約 CIN や範囲外 cable の event は受け付けません。cable 数を clamp せず、範囲外なら拒否します。
- 送信 helper も丸めません。channel が 15 超、data byte が 127 超、pitch bend が 16383 超なら、`ESP_ERR_INVALID_ARG` で何も送りません。

### HID

- report descriptor の Report Size / Count / ID を解析して、Input / Output / Feature の容量を決めます。Input だけなら OUT と control 用の領域を作りません。
- GET_REPORT は report ID と種類ごとの snapshot から返します。idle が有効なら Input を周期的に再送します（HID 1.11 §7.2.4）。相対値（Mouse の移動量など）は送信後に 0 に戻し、idle で移動を繰り返しません。
- boot / report protocol の切替では、旧形式の未送信 IN を hardware で止め、古い完了が抜けるまで送信を待ちます。Host に旧形式の report を渡さないためです。
- System Control は標準 usage 0x81 / 0x82 / 0x83 で指定します（HID Usage Tables、Generic Desktop page）。ほかの値は `ESP_ERR_INVALID_ARG` です。
- Consumer Control の usage は report の Logical Maximum の 0x3FF までです。超えたら `ESP_ERR_INVALID_ARG` で送りません。
- Keyboard の press 系は、押しているキーと修飾キーに足し込みます。修飾キーの usage（0xE0〜0xE7）は modifier の bit にします。
  NKRO なしでは修飾キー以外を 6 つまで持ちます（HID 1.11 Appendix B.1）。7 つ目は `ESP_ERR_INVALID_SIZE` で、状態を変えません。release は該当のキーだけを外して前へ詰めます。tap は自分が足したものだけを離します。
  layout で打てない文字は `ESP_ERR_NOT_SUPPORTED`、report で表せない usage（NKRO で 0xDF 超の非修飾キー）は `ESP_ERR_INVALID_ARG` です。
- Mouse の `move()` は、既定（`KeepButtons`）で押している button を保ちます。0〜255 を渡すと置き換えます。
- 各 HID の interrupt IN の wMaxPacketSize は、Function の希望の最大を、まとめた interface に使います。`EspUsbDeviceHidCustom` の `inputReportSize` と `EspUsbDeviceHidVendor` の report 長がこれに効きます。FS は 64 まで、HS は 64 を超えると 1024 までです（USB 2.0 §5.7.3）。実際の値は `inputPacketSize()` で読めます。

### MSC

- sector 長と転送 staging の容量を分け、一つの DMA buffer を command / data / status で共有します。
- `transferBytes` は bulk MPS の倍数です。理由：一回の OUT DMA で staging 全体を受けるので、末尾が packet 境界でないと、満杯の最終 packet が staging をはみ出します。
- sector 長を MPS に切り上げません。Host が最終 packet に余分な byte を含めても、次の sector へ書きません。
- 媒体の挿入（`mediaPresent(true)`）は、次の command を一度 UNIT ATTENTION（06/28/00）で失敗させて通知します。抜去は進行中の READ / WRITE を NOT READY（02/3A/00）で失敗させます（[T10 ASC/ASCQ](https://www.t10.org/lists/asc-num.htm)）。
  理由：present flag だけでは、媒体の交換を Host へ伝えられないためです。BOT reset の間も sense は保持します。
- 書込み禁止（`isWritable(false)`、`EspUsbDeviceMscSdCard::readOnly(true)`）は Host へ伝えます。MODE SENSE の WP を立て、WRITE は DATA PROTECT（07/27/00）で失敗させます。
- `EspUsbDeviceMscFatRamDisk` は容量によらず FAT12 です。cluster を 1〜128 sector の中で、cluster 数が 4085 未満になる最小の 2 の冪にします。65536 sector 以上は BPB_TotSec32 に書きます。
- SET_INTERFACE は、未完の command を捨てて次の CBW に戻ります。ただし不正な CBW や Phase Error の後に必要な BOT reset 要求は保持します（USB MSC BOT 1.0 §5.3.4 Reset Recovery）。

### Audio（UAC1 / UAC2）

- 容量は frame 単位（`bufferFrames`）で指定します。24 bit PCM でも channel の組を分断しないためです。read / write の値は byte 単位です。
- Playback のあふれの既定は `DropOldest` です。live 再生では、古い音を残すより新しい音を残す方が回復が早いためです。保持順が要る用途は `DropNewest` を指定します。捨てる単位は全 channel を含む sample frame です。
- Capture は満杯のとき上書きせず、入った byte 数を返します。
- PCM の形（subslot / bit 数）ごとに alternate を作り、rate はその一覧にまとめます。共有 clock でどの形も選べるよう、各形・両方向の rate 集合をそろえます。そろわない設定は `begin()` が `ESP_ERR_INVALID_ARG` です。
- UAC2 の Playback は explicit feedback endpoint を持ちます。値の形式は USB 2.0 §5.12.4.2 に従います。

| 速度 | 形式 | 長さ |
| --- | --- | --- |
| FS | 10.14（1 ms あたりの sample 数） | 3 byte |
| HS | 16.16（125 µs あたりの sample 数） | 4 byte |

- feedback は USB の公称 clock から作ります。外部 codec の clock への追従はしません。
- 周期処理は worker で行います。ISR へ移す根拠は実測で得られていません。

### Video（UVC）

- `submitFrame(frame, length)` は `EspUsbTransferSubmission` を返し、受理した frame を一度だけ返します。`sendFrame` は bool の簡易版です。
- 既定の transport は ISO で、ISO の payload は 512 byte です。可変長の MJPEG や、予約帯域より転送量を優先する用途では Bulk を選びます。
- descriptor の 32 bit bit rate があふれる設定は、setter で拒否します。
- 広告するのは一つの format、一つの固定 frame interval です。Probe / Commit はその範囲で受け付けます。

### CDC-NCM

- NTB の容量、方向ごとの buffer 数、datagram 数を `EspUsbNetConfig` で指定します。buffer 数 0 の方向は pool を作りません。
- `sendFrame` は NTB へ copy して worker へ渡し、満杯なら待たずに false を返します。配送完了の通知はない best effort です。TCP/IP の task から DMA を触らないためです。
- IP 層（`beginNetwork`）は別 file です。raw frame だけを使う構成には esp_netif の glue がリンクされません。
- NTB 容量の再交渉では data toggle を保持し、SET_INTERFACE では DATA0 に戻します。両者を区別します。
- HALT は IN / OUT / 通知の対象方向だけを回復させます。送受信が独立しているためです。

### CCID：`messageBytes` の既定

- 既定 `messageBytes = 0` は「自動」で、`begin()` で `max(271, 選んだ controller の Bulk-OUT MPS)` に決まります（FS 271、HS 512）。
- 271 は短い APDU 一つ分（header 10 + CLA/INS/P1/P2 4 + Lc 1 + data 255 + Le 1）です。
- CCID 1.1 §5.1 は `dwMaxCCIDMessageLength` が Bulk-OUT の wMaxPacketSize 以上であることを求めます。固定の既定値では HS（MPS 512）で必ず違反するため、自動にしました。
- 明示値は 64〜65535 byte です。MPS 未満なら `begin()` が `ESP_ERR_INVALID_SIZE` で失敗します。黙って切り上げません。
- 決まった値を `messageCapacity()` と descriptor の `dwMaxCCIDMessageLength` に出します。
- 返信は 1 本の DMA buffer を IN / ZLP の完了まで保持し、その後に次の OUT を arm します。広告している 1 slot / 1 busy command と backpressure をそろえるためです。
- `removable = false` はカード固定を表します。開始前の `insertCard()` が必須で、通知 endpoint を作りません（CCID 1.1 §3.1.2、§5.2）。
- 大きな `messageBytes` を extended APDU 対応の広告へ自動で変えません。資源の指定と protocol 機能は別です。

### DFU

- `onDetach` は SETUP の時点で受付可否を返す bool callback です。false なら STALL し、状態は appIDLE のままです。
- 理由：ACK した後の void callback では、Host へ拒否を返せません（DFU 1.1 §5.1）。
- 受け付けた後の移行（再起動など）は `setDetachHandler` で指定します。受け付けた detach は `end()` や reset でも取り消しません。
- Download の block 容量と外部 buffer は `EspUsbDfuConfig` で指定します。Runtime だけの構成は block 領域を持ちません。
- 保存先は差し替えられます（既定は OTA partition）。USB protocol と書込み先を分け、外部 flash や別 processor への転送にも使えるようにしています。

### Remote wakeup

- 能力の広告、Host の許可（SET_FEATURE(DEVICE_REMOTE_WAKEUP)）、アプリの要求を分けます。既定は広告しません（USB 2.0 §9.4.5、§7.1.7.7）。
- Host の許可は Address 状態でも受け付けます。bus reset で消えます。

## ArduinoCore-CH32RV と共通にしている仕様

ArduinoCore-CH32RV の USB Device library（CH32RVUsbDevice）とは、利用者から見た意味をそろえています。コードの共有ではありません。名前が同じだけで互換とは判断しません。

### 共通の仕様

| 項目 | 共通の仕様 |
| --- | --- |
| 構成 | Device に Function を関連付け、設定して `begin()`。再設定は `end()` → `configure()` → `begin()` |
| 状態 | `started()` は資源を持っている、`ready()` は Host が構成済み |
| 停止 | 停止が成功した後だけ buffer と Function を破棄できる |
| 借用転送 | 受理した buffer は reset / 停止 / 取消し / error でも一度だけ返す |
| CDC | `write` / `print` は TX timeout（既定 1000 ms）まで空きを待つ。`flush` は DMA と終端 ZLP まで期限付きで待つ。`setTxTimeoutMs(0)` で即時に一部だけ |
| Video | `submitFrame(frame, length)` は `EspUsbTransferSubmission` を返す。`sendFrame` は bool |
| Audio | `playbackOverflowPolicy`、既定 `DropOldest` |
| DFU | bool の `onDetach` が SETUP の時点で受付可否を返す。受け付けた後は `end()` / reset でも取り消さない |
| MSC | 挿入を 06/28/00 で一度通知、抜去は 02/3A/00 で失敗させる。BOT reset の間も sense を保持する |
| CCID | `messageBytes = 0` は自動で、`begin()` で `max(271, Bulk-OUT MPS)` に決まる。明示値が MPS 未満なら失敗 |

### 違いを残すもの

| 項目 | EspUsbDevice | CH32RV | 理由 |
| --- | --- | --- | --- |
| CDC の DTR | DTR を要求しない | DTR がなければ捨てる | ESP は Host が構成を選んでいれば送る |
| 成功後の `begin()` 再呼出し | `ESP_ERR_INVALID_STATE` | 成功扱い | 異なる設定が適用されたと誤解させない |
| `end()` の戻り値 | `esp_err_t` | void | 型より「停止の成功後だけ破棄できる」をそろえる |
| System Control の値 | 標準 usage 0x81 / 0x82 / 0x83 | 1 / 2 / 3 | 共有するアプリは各 library の名前付き定数を使う |
| buffer の確保 | `begin()` で確保 | 系列の最大 packet で静的に確保 | 各 platform の方式のまま |
| Video の既定 transport | ISO | Bulk | ESP の FS ISO の既定 packet は 512 byte で、CH32RV の 64 byte の前提と違う |
| DFU の `setDetachHandler` | あり | なし | ESP 独自 |

ESP 固有の worker / core / cache の設定と、CH32RV 固有の clock や cooperative 実行は、共通の概念に入れません。

## 関連文書

- [試験](testing.ja.md)、[リリース手順](release.ja.md)
- [DWC2 の由来と license](../../third_party/tinyusb/PROVENANCE.ja.md)
- [v2 からの移行](../migrating-from-v2.ja.md)
- [USB 2.0 specification](https://www.usb.org/document-library/usb-20-specification)
