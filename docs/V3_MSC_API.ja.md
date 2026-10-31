# EspUsbDeviceMsc API（v3）

USB Mass Storage（MSC）の Function です。BOT（Bulk-Only Transport）と SCSI のコマンドをライブラリ独自の driver で処理し、ブロックの読み書きはアプリの callback に任せます。
RAM disk、FAT の RAM disk、SD カード用の補助クラスもあります。TinyUSB の MSC class と固定 4096 byte の buffer は使いません。

## 基本の使い方

```cpp
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceMsc msc(device);
uint8_t storage[32 * 512];                   // 512 byte × 32 block
EspUsbDeviceMscRamDisk volume(storage, 32);  // block 長の既定は 512

void setup() {
  EspUsbMscConfig config;                    // transferBytes の既定 512 は FS/HS どちらでも使える
  config.name = "RAM Disk";
  if (msc.configure(config) != ESP_OK) return;
  if (!volume.attach(msc)) return;           // read/write callback と容量を設定する
  msc.mediaPresent(true);                    // 媒体を「挿入済み」にする
  device.begin();
}

void loop() {}
```

自分で callback を書く場合は、`msc.onRead()`（必須）と `msc.onWrite()`（省略すると読み取り専用）を登録し、`msc.begin(blockCount, blockSize)` で容量を設定します。この `begin(count, size)` は容量の設定だけで、転送用の領域は `device.begin()` が呼ぶ引数なしの `begin()` で確保します。
callback の書き方は [MscCdcBlocks](../examples/MscCdcBlocks/MscCdcBlocks.ino) の例を参照してください。

### 主な設定

`EspUsbMscConfig` の項目です。

| 項目 | 既定値 | 意味 | FS/HSでの違い |
| --- | --- | --- | --- |
| `transferBytes` | 512 | 転送用の DMA 領域（staging）の byte 数。64 の倍数で最大 65472 | HS では 512 の倍数が必要（最大 65024）。512 未満や 512 の倍数でない値は `begin()` が `ESP_ERR_INVALID_SIZE` |
| `transferBuffer` | `nullptr` | 利用者が用意する DMA 領域。nullptr なら begin で確保 | なし |
| `name` | `nullptr` | Function 名（UTF-8）。begin 時にコピー | なし |

停止中に設定する項目：

| 関数 | 既定値 | 意味 |
| --- | --- | --- |
| `vendorID()` / `productID()` / `productRevision()` | "ESP32" / "MSC" / "1.0" | INQUIRY で返す文字列（8 / 16 / 4 文字まで） |
| `begin(blockCount, blockSize)` | 未設定 | 容量。`attach()` を使う補助クラスはこれを呼ぶ |
| `onRead` / `onWrite` / `onStartStop` | 未登録 | storage の callback |

動作中にも変えられるもの：`mediaPresent(bool)`（既定 false＝媒体なし）、`isWritable(bool)`（既定 true）。

## 設定の変え方

- 設定を変えるときは `device.end()` → `configure()`・容量・callback の設定 → `device.begin()` の順に呼びます。再ビルドは要りません。
- `configure()`、`onRead` / `onWrite` / `onStartStop`、`vendorID` / `productID` / `productRevision` は停止中だけ `ESP_OK` を返します。動作中は `ESP_ERR_INVALID_STATE` で、それまでの設定を保ちます。
- `transferCapacity()` は開始前と終了後は 0、開始後は実際の容量を返します。

`configure()` の戻り値：

| 戻り値 | 条件 |
| --- | --- |
| `ESP_OK` | 受け付けた |
| `ESP_ERR_INVALID_STATE` | 動作中 |
| `ESP_ERR_INVALID_SIZE` | `transferBytes` が 0、65535 超、64 の倍数でない |
| `ESP_ERR_INVALID_ARG` | `transferBuffer` が 64 byte 境界でない、内部 RAM でない、DMA に使えない |

`device.begin()` の後で `msc.startError()` を見ると、MSC の開始に失敗した理由がわかります。

| `startError()` | 条件 |
| --- | --- |
| `ESP_ERR_INVALID_ARG` | 容量が未設定、または read callback が未登録 |
| `ESP_ERR_INVALID_SIZE` | HS を使う構成で `transferBytes` が 512 の倍数でない |
| `ESP_ERR_NO_MEM` | 転送領域や driver 登録の確保に失敗 |
| `ESP_ERR_INVALID_STATE` | 登録されていない、または開始済み |

## bufferと費用

- 1つの BOT コマンドでは IN と OUT が同時に動かないので、1つの DMA 領域を CBW（コマンド）、SCSI の応答とデータ、CSW（状態）に順番に使います。Function ごとの状態と必要な buffer だけを確保します。
- sector 長と `transferBytes` は独立しています。64 byte のような小さな転送でも、512 byte の sector を offset 付きで分けて callback に渡します。そのぶん callback の呼び出し回数は増えます。
- `transferBuffer` を指定すると内部の確保を省きます。64 byte 境界の、DMA に使える内部 RAM を `transferBytes` 分用意し、begin から end まで保持してください。ライブラリは解放しません。
- 1つの Function が公開するのは LUN 0 だけです。MSC Function を複数登録すると、それぞれが独立した容量・callback・buffer と bulk endpoint の組を持ちます（controller の endpoint 数の範囲内）。
- MSC を使わないプログラムには MSC 固有の処理はリンクされません。

## callbackと状態変更

callback の型：

```cpp
int32_t onRead(uint32_t lba, uint32_t offset, void *buffer, uint32_t size);
int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size);
bool onStartStop(uint8_t powerCondition, bool start, bool loadEject);
```

- read/write は同期で処理し、指定された `size` をそのまま返して完了します。負の値、0、`size` と違う値は I/O 失敗（Medium Error）になります。
- write callback がなければ読み取り専用として扱います。
- 書き込み callback は、戻る前に必要な書き込みを済ませてください。SYNCHRONIZE CACHE はこの前提で成功を返すだけで、アプリ側の cache を flush する機能ではありません。
- callback は USB worker task の上で、ライブラリの spinlock を持たずに呼ばれます。中で end、設定変更、Function の破棄をしないでください。
- `mediaPresent()` と `isWritable()` は動作中に atomic に変えられ、次の SCSI 要求の処理で反映されます。
- 動作中に Function を破棄すると Device が止まり、worker と DMA が止まってから状態を解放します。登録していない MSC を手動で begin・破棄しても、他の Function の通信は壊れません。

## 媒体の取り外しと交換

`mediaPresent()` で媒体の抜き差しを表します。void のままで、新しい通知 API は追加していません。

```cpp
msc.mediaPresent(false);  // 抜いた
// 媒体を準備する
msc.mediaPresent(true);   // 挿した
```

| 操作 | Host から見える動き |
| --- | --- |
| true → false（抜去） | 以後のコマンドは Not Ready / Medium Not Present（sense 02/3A/00）で失敗する。実行中の READ/WRITE も、残りは 02/3A/00 で失敗し、それ以上 callback は呼ばない |
| false → true（挿入、Host が構成済みの間） | 次のコマンド（INQUIRY と REQUEST SENSE を除く）を UNIT ATTENTION（06/28/00）で1回だけ失敗させる |
| 同じ値の再設定 | 変化なし |
| 開始前・未構成中の変更、初回列挙時の true | 通知しない |

- 通知前に何度挿し直しても、通知は1回にまとめます。通知前に再び抜くと、06/28/00 を1回返した後は 02/3A/00 を返します。
- 通知で失敗した後の REQUEST SENSE は 06/28/00 を返します。REQUEST SENSE 自体は失敗させません。
- 不正な CBW や存在しない LUN へのコマンドでは、未通知の挿入を消費しません。
- USB reset、構成解除（SET_CONFIGURATION）、end で未通知の挿入と sense を消します。BOT reset recovery では、未通知の挿入と直前の失敗の sense を両方残します。
- 抜去で 02/3A/00 を返しても、未通知の挿入（UNIT ATTENTION）は消費しません。

### I/Oと所有権の境界

- `mediaPresent()` は I/O を同期的に止める API ではありません。すでに実行中の callback は中断せず、その呼び出しが戻るのを待ちます。sector を完成させるために追加の callback を呼ぶことはしません。
- 抜去の後に短時間で挿し直しても、抜去の前に始まったコマンドは再開しません。新しい媒体へ古いコマンドのデータを書くことはありません。
- 成功済みの書き込みは巻き戻しません。残りがあるコマンドは成功として返しません。
- 失敗した READ の residue（CSW で返す未転送量）には、Host へ実際に送った量だけを反映します。WRITE も実際に処理した量で残量と CSW を決めます。
- 媒体が変わらない一般の backend 失敗は Medium Error のままで、02/3A/00 には置き換えません。
- `mediaPresent(false)` を呼んだだけで保存先の pointer を破棄・交換してはいけません。callback とその参照先は `device.end()` が成功するまで有効に保ってください。callback と容量の交換は停止中だけです。
- 容量や sector 長が変わる交換は、end → 再設定 → begin で行います。
- 物理的に抜ける backend（SD カードなど）は、callback の中で安全にアクセス失敗を返す必要があります。
- アプリと Host が同じ filesystem を同時に更新する問題は、この仕組みでは解決しません。OS の filesystem cache の再読み込みや自動の再 mount も保証しません。

sense の意味は [T10 の割当表](https://www.t10.org/lists/asc-num.htm)に従います。

## BOTとSCSI

対応する SCSI コマンド：INQUIRY、REQUEST SENSE、TEST UNIT READY、READ CAPACITY(10)、READ FORMAT CAPACITIES、MODE SENSE(6/10)（header のみ、optional page なし）、START STOP UNIT、PREVENT/ALLOW MEDIUM REMOVAL、SYNCHRONIZE CACHE、VERIFY（比較なし）、READ(10)、WRITE(10)。

| 状況 | sense |
| --- | --- |
| 読み取り専用への書き込み | Data Protect |
| 媒体なし | Not Ready / Medium Not Present（02/3A/00） |
| callback の失敗 | Medium Error |
| 不明なコマンド、存在しない LUN、範囲外の LBA など | 失敗として返す |

- BOT 規格の13の場合分け（Host と Device の転送方向・長さの食い違い）を扱います。
- 不正な CBW を受けると両 endpoint を halt し、Host に reset recovery を求めます。
- class reset（Bulk-Only Mass Storage Reset）ではハードウェアを止め、古い完了通知を FIFO fence（それまでの通知を処理し終えるまで待つ仕組み）で流し、両 endpoint の CLEAR_FEATURE が済むまで次の CBW を受けません。reset そのものでは DATA PID や halt を初期化せず、Host の CLEAR_FEATURE に従います。

## USB packet境界とsector境界

- 最後の OUT では bus packet 1個分の領域で受けますが、storage callback へは SCSI で要求された分だけ渡します。Host が最後の packet で余分に送った byte を次の sector へ書くことはなく、CSW の residue にも処理した storage の byte だけを数えます。
- そのため 512 の倍数でない sector 長（例：511 byte）にも対応します。sector 長を MPS に合わせて黙って切り上げることはしません。

## interfaceの再初期化とBOT回復

Host が SET_INTERFACE（alt 0）を送ると、その MSC の両方向の DMA を止め、古い完了通知を fence で流してから DATA0 に戻します。

- コマンド・データ・状態のどの段階の途中でも、未完了の BOT のやり取りを捨てて新しい CBW を待ちます。
- 保存済みの sense と媒体の設定は保持します。callback で処理済みの書き込みは巻き戻しません。Host は中断したコマンドの成功や書き込み全体の原子性を前提にできないので、必要な範囲を確認・再送してください。
- 不正な CBW や Phase Error の後の reset recovery 要求は維持します。SET_INTERFACE や単なる HALT 解除を BOT reset の代わりにはしません。BOT reset の途中で SET_INTERFACE が来ても、未完了の CLEAR_FEATURE の手順を完了扱いにしません。
- 基準は [USB-IF BOT 1.0](https://www.usb.org/sites/default/files/usbmassbulk_10.pdf) の §3.1、§5.3.4、§6.6.1 です。
- 既存の回復 job と世代付きの fence を使い、task や転送時の heap 確保は増やしていません。

## FSとHSの違い

| 項目 | FS（S3、P4 の FS controller） | HS（P4/P4X の HS controller） |
| --- | --- | --- |
| bulk の MPS | 64 byte | 512 byte |
| `transferBytes` の条件 | 64 の倍数（最小 64、最大 65472） | 512 の倍数（最小 512、最大 65024）。満たさないと `begin()` が `ESP_ERR_INVALID_SIZE` |
| 既定の `transferBytes`（512） | 使える | 使える |

P4 では `EspUsbDeviceConfig::controller` が `FullSpeed` 以外（既定の `Auto` を含む）のとき HS の条件になります。両方で動かすスケッチは `transferBytes` を 512 の倍数にしてください。
SCSI と BOT の動きは FS と HS で同じです。

## PSRAMとバッファ

| 領域 | PSRAM | 備考 |
| --- | --- | --- |
| RAM disk の保存先（`EspUsbDeviceMscRamDisk` に渡す `storage`、自作 callback の参照先） | 使える | callback で memcpy するだけなので DMA を通らない。P4 の FS loopback（64 block）と HS 直結（4 MiB、8192 block、`validation/peer/psram_link`、42d844d）の両方で確認済み |
| `transferBuffer`（DMA の staging） | 使えない | 内部 RAM で DMA 可能、64 byte 境界が必要。満たさないと `configure()` が `ESP_ERR_INVALID_ARG` |

`transferBuffer` を指定しない場合、ライブラリが DMA 可能な内部 RAM に確保します。

## 補助クラス

| クラス | 用途 |
| --- | --- |
| `EspUsbDeviceMscRamDisk(storage, blockCount, blockSize = 512)` | RAM 上の raw block。`attach(msc)` で callback と容量を設定（媒体と書込み可否の設定は変えない） |
| `EspUsbDeviceMscFatRamDisk(storage, size)` | `format()` で FAT を作り、`addFile` / `addTextFile` でファイルを置く。`onEject` で取り出しを受ける |
| `EspUsbDeviceMscSdCard(sd)` | Arduino の SD ライブラリがあるときだけ。`begin()` で SD を開き、`attach(msc)` |
| `EspUsbDeviceMscFirmwareDisk` | ファイルを書き込むとファームウェアを更新するディスク。[OTA over USB](ota-over-usb.ja.md) を参照 |

`RamDisk` と `FatRamDisk` の `attach()` は `mediaPresent(true)` を呼ばないので、`device.begin()` の前に自分で呼んでください。

## 2.x からの移行

- `CFG_TUD_MSC_EP_BUFSIZE` と `CFG_TUD_MSC`（0 を含む）は廃止しました。残っているとコンパイルエラーになります。容量は `EspUsbMscConfig` で指定します。
- `tud_msc_*` の直接呼び出しは使えません。
- callback 登録と `vendorID` などは `esp_err_t` を返す停止中の API になりました。戻り値を無視する書き方はそのまま使えますが、void のメンバー関数ポインタを使っていたコードは型の変更が必要です。
- TinyUSB の非同期完了関数や、BUSY（0 を返して保留）に頼っていた callback は、同期で全量を返す形へ直してください。
- 媒体の再挿入の直後は、最初のコマンドが 06/28/00 で1回失敗します。挿入直後の TEST UNIT READY が成功する前提のコードは直してください。

## 設計判断の理由

- **runtime 構成にした理由**：再ビルドなしで容量を変えられる、MSC を複数持てる、読み取り専用を自然に作れる、使わない処理がリンクされない。代わりに begin 時の動的確保、小さい buffer での callback 増加、同期 callback と停止中 setter への移行が必要になった。
- **媒体変更の通知**：今の挿抜の呼び方のまま Host へ変更を知らせられる。代わりに次のコマンドが意図的に1回失敗する。I/O 停止や保存先の解放まで行う API と誤解されないよう、`mediaPresent()` の役割を状態の表明に限った。追加の費用は MSC 固有の状態2語と比較だけで、監視 task や lock、媒体のコピーは加えていない。
- **抜去の仕組み**：内部では「存在 bit＋抜去回数」を1語（`media_`）で、挿入を回数（`insertions_`）で持つ。コマンドは開始時の値を覚え、各 callback の前に同じ値か（存在し、抜去がないか）を確かめる。違えば残りを 02/3A/00 で失敗させる。
- **当初案からの変更**：当初は「開始済みのコマンドは最後まで続ける」としていたが取り下げた。ArduinoCore-CH32RV 側の「いま扱っている1ブロックを最後まで」という表現に対し、ESP では sector より小さい offset 付き callback があるため、抜去後に callback を追加して sector を完成させる保証はしない。
- **BOT reset 時の sense 保持**：SCSI の状態として扱い、CH32RV コアと同じ判断にした。調整の経緯は [CH32RV との API 調整](V3_CH32RV_API_ALIGNMENT.ja.md)を参照。
- **Host の不具合への対応**：Device を Host の不具合に合わせる回避は入れない。

規格：[USB MSC BOT 1.0](https://www.usb.org/sites/default/files/usbmassbulk_10.pdf)、SCSI コマンドの参考として [USB UFI 1.0](https://www.usb.org/sites/default/files/usbmass-ufi10.pdf)

## 実測の記録

特に書かない限り S3 同士（FS）の結果です。

### driver の置き換え（2026-10-26）

- `msc_storage` は staging 64/128/512/4096 byte と外部の 192 byte、write callback なし、CDC との併用を実通信で確認。MSC 2つの分離は `msc_bot` の手動 Host が各 interface を明示的に取得し、容量と別々の payload を照合。
- BOT の13の場合分け、不正 CBW の6条件、reset 前の halt 保持、繰り返し reset、sense の取得と消去、read callback 失敗と書込み保護の後の正しい CSW・sense と読み出しの復帰も手動 Host で確認。
- EspUsbHost 2.10.1 はデータ転送の STALL ですぐ reset し、endpoint clear 後の PID 同期にも制約があった。そのため通常 API の probe ではデータの拒否を最後に確かめて通信を止める。専用の手動 Host は公開 IDF API で interface を取り直して Host 側を DATA0 にそろえ、Device の規格どおりの動きを検証する。Host library や pytest への patch・conftest は追加していない。
- 関連4試験（`msc-driver-related.xml`）は4件成功、148.36 秒。362 箇所の確保失敗と回復、未登録 MSC の拒否後の既存 HID 通信を含む。全件で両ボード STOPPED、panic なし。
- 511 byte sector で BOT の13の場合分けと、最後の余分な byte を隣の sector へ書かないことを確認（`msc-packet-boundaries.xml`、2件成功、93.48 秒）。
- 最小構成5種（Device のみ/Vendor/CDC/MIDI/MSC）の nm で、MSC 未使用の4種に MSC の実装がなく、全5種に TinyUSB の CDC/Vendor/HID/MIDI/MSC がないことを確認。MSC のみの構成には他の Function や共通 bulk stream が含まれない。BOT reset に要るハードウェアの中断処理は MSC 使用時だけリンクされる。
- 標準の `--combo all`（`runtime-msc-all.xml`）は14件成功、772.40 秒。362 箇所の失敗と回復、511/512 byte sector の各13の場合分けと隣の sector の guard、独立した2つの MSC、既存 CDC/Vendor/HID/MIDI の回帰を含む。全件で両ボード STOPPED、panic なし。記録は `validation/.pytest-results/runtime-msc-all*`。

### interface 再選択（2026-10-27）

- 通常のコマンド待ち・CSW 待ち・部分的な READ/WRITE を破棄して新しい CBW を受けることを確認。WRITE では処理済みの 64 byte が残り、残りの sector は変わらないことを実測。2つの MSC それぞれで再初期化と容量・読み戻しを確認。
- `msc-interface-verified.xml`（msc_bot、msc_storage、allocation_failure）は3件成功、252.88 秒。6つの serial log すべて STOPPED、panic なし。712 箇所の失敗と回復、heap の前後一致。
- 最初の試験で、BOT reset 中の SET_INTERFACE が HALT を早く解除する条件を見つけ、未完了の解除手順を保持するよう直した。次の失敗は Host 試験が前回の 511 byte の geometry を残していたためで、Host 開始時に 512 byte へ戻した。失敗時の記録は `.pytest-results/msc-interface-before-halt-preserve*` と `msc-interface-host-geometry*`。

### CDCとの同時通信

2026-10-29、製品 `e329ef6` のまま、独立した MSC probe に同時通信の試験を加えました。

- CDC は受信 97 / 送信 131 byte の ring、64 byte のコピー転送で 1/63/64/65/129 byte を連続で往復させ、別の Host task と main から CDC と MSC を同時に使う。Device には専用 task を足さず、loop で CDC を echo する。
- MSC は staging 64/4096 byte のそれぞれで 1/2/7/8 block を読み書きし、payload・offset・guard を比較。
- 1件成功、59.01 秒。CDC は 112/105 往復、7148/6762 byte を送り、Host の受信と Device の送受信の総 byte 数が一致。MSC の呼び出し中に CDC が完了した回数は 29/14 回で、両方を順番に成功させただけの試験ではない。
- `mediaPresent(false)` で 02/3A/00 を返している間も CDC は5往復以上進み、媒体を戻した後に MSC の payload を再照合した。回数や量は最低限の進行を確かめるための値で、性能の指標ではない。
- 既存の storage/raw BOT の2件も成功（81.69 秒）。記録は `.pytest-results/msc-cdc-concurrent*`、`msc-cdc-regression*`。

### 公開 API の例

[MscCdcBlocks](../examples/MscCdcBlocks/MscCdcBlocks.ino) は 8 KiB の RAM backing、512 byte の staging、CDC を明示的に構成する、filesystem なしの raw block の例です。S3/S2/P4 でコンパイルと依存の確認に成功し、使わない Audio/NCM/Video/CCID/keymap/FAT 補助がリンクされないことも確認しました（`.pytest-results/msc-cdc-example*`）。例そのものの実機・OS での検証はしていません。

### ESPでの実装とwire確認（2026-10-30）

`validation/p4x_pc/msc_media` は P4X を HS で Linux PC へ直結し、pyusb で CBW/CSW を直接扱います（usb-storage は切り離す）。EspUsbHost の CSW 回収の問題を通らずに sense と residue を確かめられます。

| 条件 | wire での結果 |
| --- | --- |
| 抜去、同じ値で false | TEST UNIT READY 失敗、sense 02/3A/00。同じ値では変化なし |
| 挿入 | INQUIRY 成功、REQUEST SENSE は 00/00/00 で消費なし。次の TEST UNIT READY だけ失敗して 06/28/00、その次は成功 |
| 0→1→0→1 | 通知は1件 |
| 0→1→0 | 06/28/00 の後に 02/3A/00 |
| BOT reset recovery | 未通知の挿入、直前の 02/3A/00 とも残る |
| READ(10) 4 block、1 block 目の callback 内で抜去 | 512 byte の後に IN STALL、CSW status 1・residue 1536、以後の read callback 0回、02/3A/00 |
| 同上ですぐ再挿入 | 同じく失敗し、古いコマンドは続かない。後の TEST UNIT READY で 06/28/00 を1回 |
| 4 block 目の callback で抜去 | 2048 byte、status 0 |
| WRITE(10) 4 block、1 block 目の後に抜去 | OUT STALL、status 1・residue 1536。1 block 目は書き込み済み、2 block 目は元のまま |

- 1回目の実行で1件成功（72.90 秒）。BOT reset 時の sense 保持を加えた再実行も成功。
- S3 の `msc_storage` は再挿入直後の TEST UNIT READY が成功する前提だったので、06/28 を1回確かめてから準備完了を見る形へ直した。
- MSC を使う S3/P4 の既存 probe（classes、psram、allocation_failure など）もすべて成功（12件 769 秒＋5件 151 秒）。

### CH32RV側の確認結果とHostの制約

- ArduinoCore-CH32RV の f80b4451 の差分で、抜去回数の比較、02/3A/00、失敗した READ の residue の修正を確認した。先方の X035C8 での usb-peer-classes では、2 block の READ(10) の1 block 目で抜去し、コマンドの失敗と2 block 目の read callback が呼ばれないことを確認したとの報告。02/3A/00 の wire での取得、途中の再挿入、WRITE の全条件まで実機で確認済みとは扱わない。
- 先方の EspUsbHost 2.10.0 では、データの STALL の後に CSW を取らずに終わり、後の CSW が1件ずれ、reset recovery もしないため、再接続まで通信できないという報告がある。そのため先方の抜去検査は Device を作り直す直前の最後に置かれ、sense は取れていない。先方はこれを BOT 6.7.2 に反する Host の動きとして報告しており、Device 側に互換の回避は入れていない。
- この報告と、上の ESP 側の EspUsbHost 2.10.1 での記録は、版と回復経路が違うので混同しない。標準 Host の改修は別の作業。

### HS 直結（2026-10-31）

- 上の `p4x_pc/msc_media` は P4X の HS で行った。
- `validation/peer/msc_midi` で MSC と MIDI の複合を P4/P4X の HS 直結で確認する probe を追加し、S3 の peer 試験（`msc_storage` などを含む）も HS 直結で収集した。結果は [P4/P4X と追加試験環境](V3_P4_VARIANTS_AND_BENCH.ja.md#s3-peer試験をhs直結へ広げた結果2026-10-31) を参照。

### 未確認の範囲

511/512 以外の sector 長、queue 満杯と人工的な古い完了通知の注入、OS での実ディスクの mount、SD・OTA の媒体、S2/S31、任意の Host の HALT/CLEAR だけで実行中の SCSI コマンドを続ける条件、SET_INTERFACE と bus reset/end の全到着順、他の MSC instance で同時に進むコマンドの維持、MSC の HALT 中の CDC の継続。
