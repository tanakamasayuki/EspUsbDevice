# MSC（Mass Storage）

[English](msc.md)

## 概要と使う場面

`EspUsbDeviceMsc` は、USB Mass Storage Class の Function です。Host からは USB メモリのようなドライブに見えます。
BOT（Bulk-Only Transport）と SCSI のコマンドはライブラリが処理し、ブロックの読み書きはアプリの callback に任せます。

使う場面の目安です。

- 設定ファイルやログを、ドライバなしで Host とやり取りする。
- SD カードを USB カードリーダーとして見せる。
- ファイルをコピーするだけでファームウェアを更新する（`EspUsbDeviceMscFirmwareDisk`、[OTA over USB](../ota-over-usb.ja.md)）。

すぐに使える補助クラスがあります。

| クラス | 用途 |
| --- | --- |
| `EspUsbDeviceMscRamDisk` | RAM 上の raw block（filesystem なし） |
| `EspUsbDeviceMscFatRamDisk` | RAM 上の FAT12 ディスク。ファイルを置いてから公開できる |
| `EspUsbDeviceMscSdCard` | SD カード（Arduino の SD ライブラリがあるとき） |
| `EspUsbDeviceMscFirmwareDisk` | ファイルを書くとファームウェアを更新するディスク |

1つの Function が公開するのは LUN 0 だけです。MSC Function を複数登録すると、それぞれが独立した容量・callback・buffer と bulk endpoint の組を持ちます。

動作は、ESP32-S3 同士の FS 接続、ESP32-P4 の FS loopback と HS 直結（いずれも EspUsbHost などの ESP32 を Host にした試験）、P4X と Linux PC の HS 直結（usb-storage を外し、pyusb で BOT と SCSI を直接確認）で確認しています。OS がディスクとして mount する動作は未確認です。

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
  msc.mediaPresent(true);                    // 媒体を「挿入済み」にする（既定は「なし」）
  device.begin();
}

void loop() {}
```

自分で callback を書く場合：

```cpp
msc.onRead([](uint32_t lba, uint32_t offset, void *buffer, uint32_t size) -> int32_t {
  memcpy(buffer, storage + lba * 512 + offset, size);
  return size;                               // 要求された size をそのまま返すと成功
});
msc.onWrite([](uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size) -> int32_t {
  memcpy(storage + lba * 512 + offset, buffer, size);
  return size;
});
msc.begin(32, 512);                          // 容量（block 数、block 長）。onRead の後に呼ぶ
msc.mediaPresent(true);
device.begin();
```

`msc.begin(blockCount, blockSize)` は容量の設定だけです。転送用の領域は `device.begin()` が確保します。read callback が未登録だと false を返すので、`onRead()` の後に呼びます。

## 設定

`EspUsbMscConfig` の全項目です。

| 項目 | 既定値 | 範囲・意味 | FS/HS の違い |
| --- | --- | --- | --- |
| `transferBytes` | 512 | 転送用の DMA 領域（staging）の byte 数。64 の倍数で 64〜65472 | HS を使う構成では 512 の倍数（512〜65024）が必要。満たさないと begin が `ESP_ERR_INVALID_SIZE` |
| `transferBuffer` | `nullptr` | 自分で用意する DMA 領域。`nullptr` なら begin で確保 | なし |
| `name` | `nullptr` | Function 名（UTF-8）。begin でコピーし、interface 名として出す | なし |

停止中に設定する項目：

| 関数 | 既定値 | 意味 |
| --- | --- | --- |
| `vendorID(s)` / `productID(s)` / `productRevision(s)` | "ESP32" / "MSC" / "1.0" | INQUIRY で返す文字列（8 / 16 / 4 文字まで。長い分は切り、短い分は空白で埋める） |
| `begin(blockCount, blockSize)` | 未設定 | 容量。補助クラスの `attach()` がこれを呼ぶ |
| `onRead` / `onWrite` / `onStartStop` | 未登録 | storage の callback |

動作中にも変えられる項目：

| 関数 | 既定値 | 意味 |
| --- | --- | --- |
| `mediaPresent(bool)` | false（媒体なし） | 媒体の有無（[媒体の取り外しと交換](#媒体の取り外しと交換)） |
| `isWritable(bool)` | true | 書き込みを許すか。false なら Host には書き込み禁止に見える |

- `configure()`、callback の登録、`vendorID` などは停止中だけ `ESP_OK` を返します。動作中は `ESP_ERR_INVALID_STATE` で、元の設定を保ちます。
- 設定を変えるときは `device.end()` → 設定 → `device.begin()` の順にします。

`configure()` の戻り値：

| 戻り値 | 条件 |
| --- | --- |
| `ESP_OK` | 受け付けた |
| `ESP_ERR_INVALID_STATE` | 動作中 |
| `ESP_ERR_INVALID_SIZE` | `transferBytes` が 0、65535 超、64 の倍数でない |
| `ESP_ERR_INVALID_ARG` | `transferBuffer` が 64 byte 境界でない、内部 RAM でない、DMA に使えない |

`device.begin()` の後で `msc.startError()` を見ると、MSC の開始に失敗した理由が分かります。

| `startError()` | 条件 |
| --- | --- |
| `ESP_ERR_INVALID_ARG` | 容量が未設定、または read callback が未登録 |
| `ESP_ERR_INVALID_SIZE` | HS を使う構成で `transferBytes` が 512 の倍数でない |
| `ESP_ERR_NO_MEM` | 転送領域や driver の登録に失敗 |
| `ESP_ERR_INVALID_STATE` | 登録されていない、または開始済み |

## API

### callback

```cpp
int32_t onRead(uint32_t lba, uint32_t offset, void *buffer, uint32_t size);
int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size);
bool onStartStop(uint8_t powerCondition, bool start, bool loadEject);
```

- read/write は同期で処理し、指定された `size` をそのまま返して完了します。負の値、0、`size` と違う値は I/O の失敗（Medium Error）になります。
- 1回の callback が扱うのは、block `lba` の `offset` byte 目から `size` byte です。`size` は `transferBytes` 以下で、block をまたぐことがあります。
- write callback がなければ読み取り専用として扱います。
- write callback は、戻る前に書き込みを済ませてください。SYNCHRONIZE CACHE はこの前提で成功を返すだけです。
- `onStartStop` は START STOP UNIT で呼ばれます。Host の「取り出し」は通常 `start = false`、`loadEject = true` です。false を返すとコマンドは失敗します（05/24/00）。
- callback は USB worker で、ライブラリの lock を持たずに呼ばれます。中で `device.end()`、設定変更、Function の破棄をしないでください。

### 状態の照会

| 関数 | 説明 |
| --- | --- |
| `mounted()` | Host が構成を選び、MSC が動作中 |
| `transferCapacity()` | 実際の転送領域の byte 数。開始前と終了後は 0 |
| `configuration()` | 要求した設定 |
| `testUnitReady()` | 媒体ありか |
| `writable()` | `isWritable(true)` かつ write callback がある |

### 媒体の取り外しと交換

`mediaPresent()` で媒体の抜き差しを表します。

```cpp
msc.mediaPresent(false);  // 抜いた
// 媒体を準備する
msc.mediaPresent(true);   // 挿した
```

| 操作 | Host から見える動き |
| --- | --- |
| true → false（抜去） | 以後のコマンドは Not Ready / Medium Not Present（sense 02/3A/00）で失敗する。実行中の READ/WRITE も、残りは 02/3A/00 で失敗し、それ以上 callback は呼ばない |
| false → true（挿入、Host が構成済みの間） | 次のコマンド（INQUIRY と REQUEST SENSE を除く）を UNIT ATTENTION（06/28/00）で1回だけ失敗させる。Host はこれで媒体の交換を知る |
| 同じ値の再設定 | 変化なし |
| 開始前・未構成中の変更、初回列挙時の true | 通知しない |

- 通知前に何度挿し直しても、通知は1回にまとめます。
- USB reset、構成解除、end で、未通知の挿入と sense を消します。BOT reset recovery では両方を残します。
- `mediaPresent()` は I/O を同期的に止める API ではありません。実行中の callback は中断せず、戻るのを待ちます。
- 抜去の前に始まったコマンドは、すぐ挿し直しても再開しません。新しい媒体へ古いコマンドのデータを書くことはありません。成功済みの書き込みは巻き戻しません。
- `mediaPresent(false)` を呼んだだけで保存先を破棄・交換してはいけません。callback とその参照先は `device.end()` が成功するまで有効に保ってください。容量や block 長が変わる交換は、end → 再設定 → begin で行います。
- 物理的に抜ける媒体（SD カードなど）は、callback の中でアクセスの失敗を返せるようにしてください。

### SCSI コマンドと sense

対応する SCSI コマンド：INQUIRY、REQUEST SENSE、TEST UNIT READY、READ CAPACITY(10)、READ FORMAT CAPACITIES、MODE SENSE(6/10)（header のみ。書き込み禁止の bit を返す）、START STOP UNIT、PREVENT/ALLOW MEDIUM REMOVAL、SYNCHRONIZE CACHE、VERIFY(10)（比較なし）、READ(10)、WRITE(10)。

| 状況 | sense（key/ASC/ASCQ） |
| --- | --- |
| 媒体なし | 02/3A/00 Medium Not Present |
| 媒体の交換を通知 | 06/28/00 Not Ready to Ready Change |
| read callback の失敗 | 03/11/00 Unrecovered Read Error |
| write callback の失敗 | 03/0C/00 Write Error |
| 読み取り専用への書き込み | 07/27/00 Write Protected |
| 範囲外の LBA | 05/21/00 |
| 不明なコマンド | 05/20/00 |
| CDB の不正な field、`onStartStop` が false | 05/24/00 |
| 存在しない LUN | 05/25/00 |

- INQUIRY は direct access、removable（RMB = 1）として答えます。
- PREVENT/ALLOW MEDIUM REMOVAL は成功を返すだけで、`mediaPresent(false)` を止めるものではありません。
- READ(10)/WRITE(10) の block 数の上限は CDB の 16 bit、容量は READ CAPACITY(10) の 32 bit の範囲です（2 TiB 未満、512 byte block の場合）。

## BOT の処理

- BOT 規格の13の場合分け（Host と Device の転送方向・長さの食い違い）を扱い、CSW の residue には実際に処理した量を入れます。
- 不正な CBW を受けると両 endpoint を HALT し、Host に reset recovery（Bulk-Only Mass Storage Reset と両 endpoint の CLEAR_FEATURE）を求めます（BOT 1.0 §5.3、§6.6.1）。
- class reset では転送を止め、両 endpoint の CLEAR_FEATURE が済むまで次の CBW を受けません。
- SET_INTERFACE（alt 0）では、どの段階の途中でも未完了のやり取りを捨てて DATA0 に戻し、新しい CBW を待ちます。sense と媒体の設定は保ちます。
- block 長は `transferBytes` と独立です。512 の倍数でない block 長（例：511 byte）も扱えます。Host が最後の packet で余分に送った byte を次の block へ書くことはありません。

## FS と HS の違い

| 項目 | FS（S3、P4 の FS controller） | HS（P4/P4X の HS controller） |
| --- | --- | --- |
| bulk の MPS | 64 byte | 512 byte |
| `transferBytes` の条件 | 64 の倍数（64〜65472） | 512 の倍数（512〜65024） |
| 既定の `transferBytes`（512） | 使える | 使える |

- P4 では `EspUsbDeviceConfig::controller` が `FullSpeed` 以外（既定の `Auto` を含む）のとき HS の条件になります。両方で動かすスケッチは `transferBytes` を 512 の倍数にしてください。
- SCSI と BOT の動きは FS と HS で同じです。
- MSC 1つで非 control の IN endpoint を1本使います。

## バッファとメモリ

| 領域 | PSRAM | 備考 |
| --- | --- | --- |
| 保存先（`EspUsbDeviceMscRamDisk` に渡す `storage`、自作 callback の参照先） | 使える | callback で memcpy するだけで、DMA を通らない |
| `transferBuffer`（DMA の staging） | 使えない | DMA 可能な内部 RAM、64 byte 境界が必要。満たさないと `configure()` が `ESP_ERR_INVALID_ARG` |

- 1つの BOT コマンドでは IN と OUT が同時に動かないので、1つの DMA 領域を CBW、データ、CSW に順番に使います。
- `transferBytes` を小さくするとメモリは減りますが、callback の呼び出し回数が増えます。
- `transferBuffer` を指定すると内部の確保を省きます。begin から end まで有効に保ってください。ライブラリは解放しません。
- RAM disk の保存先を PSRAM に置く構成は、P4 の FS loopback と HS 直結で確認しています。
- MSC を使わないプログラムには、MSC の実装はリンクされません。

## 補助クラス

### EspUsbDeviceMscRamDisk

`EspUsbDeviceMscRamDisk(storage, blockCount, blockSize = 512)`。RAM 上の raw block です。

- `attach(msc)` で read/write callback と容量を設定します。媒体の有無と書き込み可否の設定は変えないので、`device.begin()` の前に `msc.mediaPresent(true)` を呼びます。
- どの補助クラスの `attach()` も、callback を登録できたかを確かめます。Device の稼働中は登録を拒むので、`attach()` は false を返します。`device.begin()` の前か `device.end()` の後に呼んでください。
- `readBlock()` / `writeBlock()` / `clear()` で、アプリからも中身を読み書きできます。Host が同時に使っている間の整合は保証しません。
- filesystem はありません。Host は「フォーマットされていない」と扱います。

### EspUsbDeviceMscFatRamDisk

`EspUsbDeviceMscFatRamDisk(storage, size)`。RAM 上に FAT12 のディスクを作ります。

```cpp
uint8_t disk[64 * 1024];
EspUsbDeviceMscFatRamDisk fat(disk, sizeof(disk));
fat.format("ESPUSB");
fat.addTextFile("README.TXT", "hello\n");
fat.attach(msc);
msc.mediaPresent(true);
```

- `format(label)` は 512 byte sector、root entry 64個の FAT12 を作ります。16 sector 未満は false です。ラベルは boot sector と、FAT の仕様どおり root directory の volume label の項目（64 個のうち 1 個を使う）に書きます。空か null のラベルは `NO NAME` とし、項目は作りません。
- 容量によらず FAT12 です。FAT12 の cluster 数は 4085 未満なので、`format()` はそれに収まる最小の cluster（1、2、4…128 sector）を選びます。PSRAM などの大きな RAM disk も、正しい FAT12 になります。128 sector/cluster でも収まらなければ false です。
- 65536 sector 以上の volume は、大きさを BPB_TotSec32 に書きます（BPB_TotSec16 は 0）。
- ファイル名は 8.3 形式（英数字と `_` `-` `~`、大文字に変換）で、ルートディレクトリだけです。`addFile` / `addTextFile` は連続した cluster に書きます。
- `exists()` / `fileSize()` / `readFile()` はルートディレクトリの 8.3 名で探します。`readFile()` は volume の中の cluster だけを辿ります。Host が FAT を壊しても、領域の外は読みません。
- `onEject(cb)` は、Host が START STOP UNIT で停止（`start = false`）を送ったときに呼ばれます。
- `attach()` は `mediaPresent(true)` を呼びません。自分で呼んでください。

### EspUsbDeviceMscSdCard

Arduino の SD ライブラリがあるときだけ使えます。

```cpp
EspUsbDeviceMscSdCard card;            // 既定は SD
if (card.begin(SS) && card.attach(msc)) msc.mediaPresent(true);
```

- `begin(ssPin, spi, frequency, mountpoint, maxFiles)` で SPI の SD を開きます。sector 長が 512 byte でなければ失敗します。
- `attach(msc)` は read/write/start-stop の callback と容量を設定します。`mediaPresent(true)` は自分で呼びます。
- `readOnly(true)` は、Host へ書き込み禁止（Write Protect）として伝えます。MODE SENSE は WP bit を返し、書き込みは DATA PROTECT（07/27/00 Write Protected）で失敗します。`attach()` の前でも後でも設定できます。
- `attach()` は Device の稼働中は失敗します。
- 同じ SD を ESP32 側の filesystem と Host が同時に書くと、どちらかの内容が壊れます。Host に公開している間は ESP32 側から書かないでください。

### EspUsbDeviceMscFirmwareDisk

ファイルを書き込むとファームウェアを更新するディスクです。`attach()` は媒体ありと書き込み可を設定します。使い方は [OTA over USB](../ota-over-usb.ja.md) を参照してください。

## Host OS 側の扱い

MSC は各 OS の標準ドライバで動きます。下の内容は各 OS の一般的な仕組みです。この版のライブラリで OS がディスクとして mount する動作は未確認です。

| OS | 標準ドライバ | 見え方 |
| --- | --- | --- |
| Windows | USBSTOR | リムーバブルディスク（ドライブ文字） |
| Linux | usb-storage | `/dev/sd*`（デスクトップ環境が自動で mount することがある） |
| macOS | IOUSBMassStorageDriver | Finder のボリューム |

- **filesystem は Host が管理します。** Host が mount している間に ESP32 側で中身を書き換えても、Host は自分の cache を使い続けるので、変更が見えなかったり、filesystem が壊れたりします。ESP32 側で変えるときは、`mediaPresent(false)` → 変更 → `mediaPresent(true)` として、Host に媒体の交換を知らせてください。ただし OS が自動で読み直すか、再 mount するかは OS 次第で、保証しません。
- **書き込みは Host の cache に溜まります。** Host のアプリがファイルを閉じても、すぐに USB で書き込まれるとは限りません。Host 側で「取り出し」や `sync` をしてから電源を切るよう案内してください。
- raw block（filesystem なし）を公開すると、Windows や macOS はフォーマットを促します。
- 媒体なし（`mediaPresent(false)`）で列挙すると、Host によっては TEST UNIT READY を繰り返し、複合装置の他の interface の準備が遅れることがあります（TinyUSB の Host の一部の版で見られた振る舞い）。
- Linux で usb-storage を外して BOT を直接扱う確認には、pyusb などで interface を取得します。

## 規格との対応と設計の理由

- **descriptor**：interface class 0x08、subclass 0x06（SCSI transparent command set）、protocol 0x50（BOT）。bulk IN/OUT の1組です（BOT 1.0 §4）。
- **BOT**：CBW/CSW の形式と13の場合分けは BOT 1.0 §5〜§6、reset recovery は §5.3.4 に従います。SET_INTERFACE や HALT の解除を BOT reset の代わりにはしません。
- **sense**：T10 の ASC/ASCQ 割当表に従います。媒体の交換は 06/28/00 で1回だけ知らせます。
- **runtime 構成にした理由**：再ビルドなしで容量を変えられる、MSC を複数持てる、読み取り専用を自然に作れる、使わない処理がリンクされない。代わりに begin での動的確保、小さい buffer での callback の増加、同期 callback への移行が必要です。
- **`mediaPresent()` を状態の表明に限った理由**：I/O の停止や保存先の解放まで行う API と誤解されないようにするためです。追加の費用は MSC の状態 2 語と比較だけで、監視 task や lock は加えていません。
- **Host の不具合に合わせない**：Device は規格どおりに動き、Host の不具合を回避するための特別な動きは入れていません。

## v2 からの変更点

- `CFG_TUD_MSC` と `CFG_TUD_MSC_EP_BUFSIZE` は廃止しました。残っているとコンパイルエラーになります。容量は `EspUsbMscConfig` で指定します。
- `tud_msc_*` の直接呼び出しは使えません。
- callback の登録と `vendorID` などは `esp_err_t` を返し、停止中だけ設定できます。
- TinyUSB の非同期完了や、BUSY（0 を返して保留）に頼っていた callback は、同期で全量を返す形へ直してください。
- 媒体を挿し直した直後は、最初のコマンドが 06/28/00 で1回失敗します。挿入直後の TEST UNIT READY が成功する前提のコードは直してください。

詳しくは [v2 からの移行](../migrating-from-v2.ja.md) を参照してください。

## 関連する examples

| example | 内容 |
| --- | --- |
| [MSC](../../examples/MSC/) | RAM disk（raw block） |
| [MSCFatRamDisk](../../examples/MSCFatRamDisk/) | ファイルを置いた FAT の RAM disk |
| [MSCSdCard](../../examples/MSCSdCard/) | SD カードリーダー |
| [FirmwareMSC](../../examples/FirmwareMSC/) | ファイルのコピーでファームウェアを更新 |
| [CompositeHidCdcMsc](../../examples/CompositeHidCdcMsc/) | HID と CDC、MSC の複合 |

規格：[USB MSC BOT 1.0](https://www.usb.org/sites/default/files/usbmassbulk_10.pdf)、[T10 ASC/ASCQ の割当](https://www.t10.org/lists/asc-num.htm)
