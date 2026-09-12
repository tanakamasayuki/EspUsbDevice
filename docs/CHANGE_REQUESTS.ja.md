# 改修依頼への回答

対象: `wch-protocols` の `references/espusbdevice-change-requests.ja.md`
（2026-09-12 版、ESP32-P4 の USB 2.0 HS 実測 E069〜E078 から起票された CR-1〜CR-9）。

**9 件すべてこちらで実機確認まで済ませた。** 依頼側の実験台（`esp32-p4-30eda0e31478`）を
借り、OTG を usbip でこの PC に引き込んで測っている。Windows の driver bind も同じボードで
確認した。**そちらで再実行してもらう必要のある項目は残っていない。**

## 一覧

| | 内容 | 結果 |
|---|---|---|
| [CR-1](#cr-1-ms-os-20-の-subset-構造) | MS OS 2.0 の subset 構造 | **修正・Windows 実機で確認**。対照実験で旧構造の Code 28 も再現 |
| [CR-2](#cr-2-control-request-の観測-hook) | control request の観測 hook | **実装**。`EspUsbDevice::onAnyControlRequest()` |
| [CR-3](#cr-3-per-speed-の-endpointsize) | per-speed の `endpointSize` | **修正**。FS 側が 64 になる |
| [CR-4](#cr-4-vendor-の-fifo-深さ) | vendor の FIFO 深さ | **実装・実測**。素のライブラリで 9.83 → 21.56 MB/s |
| [CR-5](#cr-5-帯域のばらつき) | 帯域のばらつき | **原因を特定・実測**。ZLP と転送往復の 2 つ |
| [CR-6](#cr-6-arduino-cli-と-symlink) | arduino-cli と symlink | **文書化** |
| [CR-7](#cr-7-転送を-2-つ以上-in-flight-に) | 転送を 2 つ以上 in-flight に | **不要と判明**。転送長を伸ばすほうが効き、TinyUSB は無改造 |
| [CR-8](#cr-8-hid-の-packet-size) | HID の packet size 64 B 固定 | **実装・実測**。511 byte report で 4.03 MB/s、欠落 0 |
| [CR-9](#cr-9-fifo-空き待ちの-api) | FIFO 空き待ちの API | **実装・実測**。spin 25,000 回 → 0、帯域は同じ |

測定条件は E069 に合わせた。1 run 4 MiB、64 KiB の internal RAM パターン、送出 task を
core 0 に pin して priority 5、host の read size 1 MiB、パターンはホスト側で全 word 検証。
ハーネスは [`tests/manual/p4_hs_stream`](../tests/manual/p4_hs_stream)、
[`tests/manual/p4_hs_hid_stream`](../tests/manual/p4_hs_hid_stream)、
[`tests/manual/windows_winusb`](../tests/manual/windows_winusb) として残してある。

---

## CR-1 MS OS 2.0 の subset 構造

**仮説は正しかった。Windows 実機で対照実験まで取れた。**

configuration subset / function subset を解決するのは **usbccgp.sys** で、Windows がそれを
読み込むのは composite device のときだけである。単一 interface の device に function subset を
付けると compatible ID は結び付く先を持たない。`setupapi.dev.log` に 1 行も書かれないのは、
install がそもそも始まっていないからで、device 側が 178 byte を正しく返していたのも仮説どおり。
**バイト列ではなく構造の問題だった。**

同じボード・同じファーム（レイアウトのフラグだけ違う）・毎回新しい device instance での実測:

| `msOs20Layout` | Status | compatible ID | service |
|---|---|---|---|
| **AUTO（interface 1 本なので flat, 162 byte）** | **OK / CM_PROB_NONE** | **`USB\MS_COMP_WINUSB` あり** | **WinUSB**（oem110.inf） |
| SUBSETS（従来の構造, 178 byte） | Error / **CM_PROB_FAILED_INSTALL** | なし | なし |

`CM_PROB_FAILED_INSTALL` が Code 28 であり、compatible ID が消えているところまで、
報告された症状と一致する。

```cpp
EspUsbDeviceConfig config;
config.webusbEnabled = true;
config.msOs20Layout = ESP_USB_DEVICE_MS_OS_20_AUTO;     // 既定
// ESP_USB_DEVICE_MS_OS_20_FLAT     … 常に set header 直下（162 byte）
// ESP_USB_DEVICE_MS_OS_20_SUBSETS  … 常に subset 入り（178 byte）
```

`AUTO` は `bNumInterfaces` で決める。1 本なら flat、2 本以上なら subsets。
結果は `EspUsbDevice::microsoftOs20UsesSubsets()` が返す。

**再現するときの注意（依頼書には無いが、これに何度か引っかかった）。** Windows は
device instance を VID / PID / **serial** で識別し、**失敗した driver match はその instance に
貼り付いて二度と再評価されない**。一度 Code 28 になった serial のまま試すと、descriptor を
直してもキャッシュされた失敗が返る。検証のたびに未使用の serial を使うこと。
`tests/manual/windows_winusb` はそれを手順に書いてある。

---

## CR-2 control request の観測 hook

`EspUsbDevice::onAnyControlRequest()` を追加した。戻り値で挙動は変わらない純粋な観測用で、
依頼にあったフィールドをそのまま持つ。

```cpp
device.onAnyControlRequest([](const EspUsbDeviceControlRequestInfo &r) {
  Serial.printf("%s type=0x%02x req=0x%02x val=0x%04x idx=0x%04x len=%u -> %u%s\n",
                r.stage == ESP_USB_DEVICE_CONTROL_STAGE_SETUP ? "SETUP" : "ACK",
                r.bmRequestType, r.bRequest, r.wValue, r.wIndex, r.wLength,
                r.responseLength, r.handled ? "" : " (unhandled)");
});
```

`responseLength` は**ライブラリがスタックへ渡したバイト数**（`wLength` で切り詰められる前）。
descriptor、WebUSB URL、MS OS 2.0 set のときだけ非ゼロ。

**どこで拾っているか。** 同梱 TinyUSB は upstream と byte 単位で一致していることを
`tools/verify_tinyusb_vendor.py` が検査しているので `device/usbd.c` には手を入れていない。
代わりに 2 か所。

- **vendor request は `tud_vendor_control_xfer_cb` の SETUP。** ライブラリが WebUSB /
  MS OS 2.0 を処理する前に通る。**STALL した要求も見える**のはこちらだけで、
  「来たが断った」と「そもそも来ていない」を区別するために意図して SETUP で報告している。
- **それ以外は `dcd_edpt0_status_complete` の ACK。** `usbd.c` が weak で置いていて dwc2 が
  使っていないフックで、標準要求や class 要求はここに来る。status stage が完了した要求だけなので
  **STALL した標準要求は出ない**——これが唯一の穴で、usbd.c を触らずに塞ぐ方法は見つからなかった。

CR-1 の切り分けに使うなら探すのは `type=0xC0 req=<bMS_VendorCode> idx=0x0007` の SETUP 行。
P4 の loopback（`tests/loopback/usb_vendor`）で、host 役が実際に投げた MS OS 2.0 要求を
この hook が 162 byte 付きで観測するところまで自動テストにしてある。

---

## CR-3 per-speed の `endpointSize`

指摘のとおり `(void)endpointSize;` で捨てていた。渡ってくる per-speed の値を上限として使う。

- FS 側 configuration / OTHER_SPEED_CONFIGURATION: `min(constructor 値, 64)`
- HS 側: 従来どおり 512

constructor に 64 未満を渡した場合（`EspUsbDeviceVendor v(device, 32)`）はそのまま 32 になる。
per-speed 値は**上限であって置き換えではない**。

---

## CR-4 vendor の FIFO 深さ

`src/internal/EspUsbTinyUsbConfig.h` の **class buffer を全部 `#ifndef` で囲った**。
vendor TX/RX だけでなく CDC / MIDI / MSC / HID も同じで、sketch の `build_opt.h` から
`-DCFG_TUD_VENDOR_TX_BUFSIZE=16384` のように上書きできる（`build_opt.h` は `.ino` と同じ場所、
変更したら `--clean`）。

**64 KiB が壊れた理由が分かった。** `tu_edpt_stream_init()` は FIFO サイズを **`uint16_t`** で
受け取る（`src/common/tusb_private.h`）。65536 は 0 に切り詰められ depth 0 の FIFO になる。
`usb_ready=1` だが `mounted=0` という症状と一致する。さらに `tu_fifo` は index を
`[0, 2*depth)` で回すので上限は 32768。**32768 を超える指定はビルドエラーにした。**

**P4 の既定は 4096 にした。** 8 KiB ではない。理由は CR-7 で、FIFO だけ深くするのは
効きが半分以下だと分かったため。素のライブラリ（`build_opt.h` なし）での実測:

| | median | min–max | stalls | RAM (global) |
|---|---:|---|---:|---:|
| 旧既定（512） | 9.83 MB/s | 8.33–10.21（±10%） | 32k–48k | 74,008 |
| 中間（8192 / 転送 512） | 10.76 MB/s | 10.50–11.06（±2.6%） | 26k–30k | 81,688 |
| **新既定（4096 / 転送 4096）** | **21.56 MB/s** | 19.98–22.44 | 23k–27k | **81,176** |

依頼書の「8 KiB で 10.5 MB/s 前後、stall が 3 万を切る」という合格条件は中間行で満たしている
（10.76 MB/s、26k–30k）。新既定はそこからさらに倍になり、しかも**中間行より RAM が少ない**。

---

## CR-5 帯域のばらつき

**原因は 2 つあり、片方は実測で直接見えた。**

### 1. ZLP がホストの URB を早期終了させている（実測済み）

`tu_edpt_stream_write_zlp_if_needed()`（`src/tusb.c`）は「FIFO が空」かつ「直前の転送長が
mps の倍数」のときに ZLP を送る。転送単位が 512 byte ちょうどである以上、
**送出が一瞬でも途切れるたびに毎回条件が成立する**。host 側の bulk read は short packet で
URB が完了するので、その都度 URB 再投入の往復が入る。

ホスト側で「短く返った URB の数」を数えたところ、**遅い run と完全に相関した**。
旧既定（FIFO 512）の 9 run:

| MB/s | 短く返った URB | device 側 stalls |
|---:|---:|---:|
| 10.21 | 9 | 31,942 |
| 10.06 | 10 | 33,412 |
| 9.89 | 10 | 34,272 |
| 8.81 | 51 | 42,764 |
| 8.68 | 42 | 44,283 |
| 8.33 | 47 | 47,865 |

FIFO を 4096 以上にすると**この数は全 run で 0 になり、ばらつきも ±10% から ±6% に縮む**。
「`stalls` と帯域が逆相関する」という観測は、`stalls`（FIFO が空だった回数）がそのまま
ZLP の回数だったということで説明が付く。

### 2. core 内蔵 stack とは DWC2 の転送モードが違う

- core 内蔵: `CFG_TUD_DWC2_DMA_ENABLE` をどこでも定義していない（precompiled libs の
  `tusb_config.h` にも `sdkconfig` にも無い）。TinyUSB 既定は 0 なので
  `CFG_TUD_DWC2_SLAVE_ENABLE = 1`、つまり **slave mode**。
- `EspUsbDevice`: `CFG_TUD_DWC2_DMA_ENABLE 1` / `CFG_TUD_DWC2_SLAVE_ENABLE 0` で **DMA mode**。
  2.1.0 で意図して切り替えた。slave mode の FIFO 再充填経路が CDC-NCM の device→host を
  数秒で恒久停止させたためで、戻す選択肢は無い。

slave mode は FIFO-empty 割り込みから **ISR コンテキストで**次の packet を押し込むので、
転送の継ぎ目がタスクスケジューリングに依存しない。DMA mode は「完了割り込み → event queue →
usbd タスク起床 → 再 arm」という**タスク往復**になり、この往復時間は同じ CPU で回っている
送出タスクの都合で変わる。「速さは互角、ばらつきだけ違う」という形と噛み合う。

**これは CR-7 の答えと同じ根で、そちらで実質的に解消する。** 転送を長くすれば往復の回数が
8 分の 1 になるので、往復のばらつきが帯域に出る割合もそれだけ小さくなる。

---

## CR-6 arduino-cli と symlink

`docs/troubleshooting.md` / `.ja.md` の「4. ビルドと書き込み」に項目を足した。
併せて `build_opt.h` が効かないように見えるとき（置き場所と `--clean`）も同じ章に書いた。

---

## CR-7 転送を 2 つ以上 in-flight に

**答え: device 側には要らない。1 転送を長くすれば同じものが手に入り、TinyUSB は無改造でよい。**

TinyUSB の vendor class が endpoint ごとに 1 転送しか投げないのは指摘のとおりである。
ただし**1 転送 = 1 packet ではない。** 実際に投げる長さを決めているのは

```c
// class/vendor/vendor_device.h
#ifndef CFG_TUD_VENDOR_TX_EPSIZE
  #define CFG_TUD_VENDOR_TX_EPSIZE TUD_EPSIZE_BULK_MAX   // HS では 512
#endif
```

で、`vendord_open()` はこれを `tu_edpt_stream_open()` の `xfer_len` として渡す。
**つまり既定では「1 転送 = 512 byte = 1 packet」で、毎パケットごとに完了割り込みと
usbd タスク往復が入っていた。** 1 microframe あたり 2.4 transaction という実測は
1 往復あたり約 52 us という意味になり、46 us の線上時間に対して往復のほうが長い。

DWC2 は 1 転送で複数 packet を扱える（`DIEPTSIZ` の pktcnt は 10 bit）。振ってみた結果:

| FIFO | 転送長 | median MB/s | RAM (global) |
|-----:|-------:|------------:|-------------:|
| 8192 | 512 | 10.76 | 81,688 |
| 8192 | 1024 | 14.87 | 82,200 |
| 8192 | 2048 | 18.64 | 83,224 |
| 8192 | 4096 | 20.99 | 85,272 |
| 8192 | 8192 | **22.81** | 89,368 |
| 8192 | 16384 | 22.78 | 97,560 |
| **4096** | **4096** | **21.12** | **81,176** |
| 16384 | 8192 | 23.28 | 97,560 |
| 32768 | 8192 | 23.34 | 113,936 |

**8192 で飽和する。** そして 4096/4096 は、**FIFO だけ 8 KiB にした構成より RAM が少ないのに
倍出る**。P4 の既定をそこにした。最後の 8% が欲しいなら `build_opt.h` で 8192/8192 にできる。

### host 側も測った（依頼書にあった E079 相当）

「PC 側が URB を 1 本ずつしか投げていないのが効いている可能性」も同じ台で切り分けた。
libusb の async で URB を複数 in-flight にして同じ 4 MiB を読む:

| host URB depth | median MB/s |
|---:|---:|
| 1 | 18.64 |
| 2 | 22.68 |
| 4 | 22.69 |
| 8 | 22.87 |

**depth 2 で飽和し、そこから先は device 側の天井（22.8〜23.3）に張り付く。**
EspUsbHost 側で観測された「depth 2 あれば上限に張り付く」と同じ形である。

**結論.** device の天井は約 23 MB/s で、新既定の 21.5 MB/s はその 93%。
ここに device 側の in-flight 2 本を足しても残りは数 % しかない。**CR-7 は取り下げてよい。**
TinyUSB の class driver に手を入れる話は、少なくともこの経路では割に合わない。

（RX 方向は別で、`vendord_open()` は OUT の転送長を `CFG_TUD_VENDOR_RX_NEED_ZLP` が 0 のとき
packet size 固定にしている。host→device を同じように伸ばすにはそのフラグが要り、
それはプロトコル上の意味が変わる——host が ZLP で終端する約束になる——ので既定は動かしていない。）

---

## CR-8 HID の packet size

3 点とも入れた。

1. `CFG_TUD_HID_EP_BUFSIZE` に `#ifndef` ガードを付けた。
2. `EspUsbDeviceHidVendor::begin()` と `configurationDescriptor()` の 64 固定をやめた。
   上限は `CFG_TUD_HID_EP_BUFSIZE - 1`（`tud_hid_n_report()` が先頭 1 byte を report ID に
   使うため）。`EspUsbDeviceHidVendor::maxReportSize()` で取れる。
3. **P4 の既定を 512 にした。** ちなみに **core 内蔵 stack は P4 で元から 512 だった**
   （`CONFIG_TINYUSB_HID_BUFSIZE=512`）。64 に縛っていたのはこのライブラリだけである。

**依頼書に無かったが必要だった修正がある。** `VENDOR_REPORT_DESCRIPTOR` は
**Report Count (63) が焼き込まれた定数**だった。endpoint だけ 512 にしても、report descriptor が
63 byte と宣言したままでは**ホストは 64 byte しか読まない**。E073 は host が `EspUsbHost` の
raw transfer だったので通ったが、**依頼書にある「Linux の hidraw で確かめられる」は
このままでは成立しない。** report descriptor を instance ごとに組み立て、Report Count が
`reportSize` に追従するようにした（255 超は 2 byte 形式 `0x96 lo hi`）。

### 実測（PC を host にして）

`EspUsbDeviceHidVendor hid(device, 511);` だけの device を HS で列挙させた。
**build_opt.h は不要**（P4 の既定が 512 なので）。

- 現在（HS）の configuration: interrupt endpoint `mps=512 bInterval=1`（IN / OUT とも）
- OTHER_SPEED（FS）の configuration: **`mps=64`** — per-speed の作り分けが効いている
- HID report descriptor: 32 byte、**Report Count = 511**
- Linux の HID core が bind して `/dev/hidraw0` ができる。読めた report 長は 512 の 1 種類のみ
- **4.03 MB/s / 7,866 report/s、sequence の欠落 0**

依頼書の合格条件「512 B で 4.1 MB/s 前後」に一致する。

**ただしレートはホストの URB 深さで決まる。** 同期読み 1 本ずつ（hidraw も pyusb も）では
約 1,100 report/s しか出ない。device は 8 本以上投げてもらえば 8,000/s（1 microframe に 1 report）の
天井の 98% まで出る:

| host URB depth | report/s | MB/s |
|---:|---:|---:|
| 1 | 1,177 | 0.60 |
| 2 | 2,202 | 1.13 |
| 4 | 4,045 | 2.07 |
| 8 | 6,025〜7,866 | 3.08〜4.03 |
| 16 | 7,150 | 3.66 |

`bInterval` は従来どおり 1 のままにした。HS では 125 us 周期という意味になり、
E073 の 8,046 report/s はこれによる値なので、4（= 1 ms）に「直す」と 8 分の 1 になる。

---

## CR-9 FIFO 空き待ちの API

両方入れた。

```cpp
size_t EspUsbDeviceVendor::writeAvailable() const;          // tud_vendor_n_write_available()
static size_t EspUsbDeviceVendor::writeCapacity();          // CFG_TUD_VENDOR_TX_BUFSIZE
bool EspUsbDeviceVendor::waitWritable(size_t bytes, uint32_t timeoutMs = 100);
```

`waitWritable()` は内部で binary semaphore を待ち、`tud_vendor_tx_cb` で give する。
semaphore は**最初に呼ばれたときに作る**ので、spin のままの sketch は何も払わない。
`bytes` は `writeCapacity()` で clamp する。`timeoutMs == 0` は非ブロッキングの 1 回確認。
mount されていなければ false（誰も FIFO を掃き出さないので）。

### 実測で見つかったバグ（実測しなければ出荷していた）

最初の実装は **1.85 MB/s しか出なかった**（spin の 21.12 に対して）。原因は give の順序である。

TinyUSB は `vendord_xfer_cb` で **FIFO を次の転送へ吸い出す前に** `tud_vendor_tx_cb` を呼ぶ。
その瞬間、待っている側が欲しい空きはまだ存在しない——完了した転送のバイトは arm 時に
FIFO を出ており、FIFO には sketch が後ろに積んだぶんが残ったままである。ここで起こすと
待機側は「まだ空いていない」と見てもう一度寝るので、**意味のある起床を 1 回空振りで使い切る**。
デュアルコアでは、usbd タスクが数命令先の再充填に到達する前に待機側が本当にその確認を走らせる。

`handleTxComplete()` で give の前に FIFO を吸い出すようにした。TinyUSB 自身の再充填は
1 行あとで空の FIFO を見て何もしない。待ち時間の刻みも 10 ms から 2 ms にした
（刻みは「空振りした起床の代償」でもあるため）。

| | median | stalls | waits |
|---|---:|---:|---:|
| spin（従来どおり） | 21.12 MB/s | 約 25,000 | — |
| `waitWritable()`（修正前） | 1.85 MB/s | 0 | 899 |
| **`waitWritable()`（修正後）** | **20.94 MB/s** | **0** | 1,022 |

**帯域は spin と同じまま、spin が 25,000 回から 0 になる。** 1 転送につき 1 回だけ block する。
依頼書の「`stalls` が桁で減る」はこれで満たしている。E078 のように capture しながら流す用途では、
この 25,000 回が harvest task と取り合っていた CPU がそのまま空く。

---

## 作業中に見つかった別件

**複合 HID に `EspUsbDeviceHidVendor` を混ぜると report descriptor が壊れる。**
`EspUsbDevice::buildDescriptors()` の複合 HID merge は、各クラスの report descriptor の
**先頭 6 byte を prologue と決め打ち**して、その後ろに Report ID item を差し込む。
keyboard / mouse は `05 xx 09 xx a1 01` でちょうど 6 byte だが、vendor HID の usage page は
3 byte item（`06 00 ff`）なので prologue が 7 byte あり、しかも Report ID を自前で持っている。
結果、6 byte で切った続きに Report ID を挿すと item 境界がずれる。

**今回は直していない。** CR の範囲外であり、`EspUsbDeviceHidVendor` は examples / tests とも
単独でしか使われていないので現に踏まれていない。直すなら merge 側を item 単位で走査する形に
変える必要があり、複合 HID 全体の回帰確認が要る。別途起票する。

---

## 確認したこと

| 対象 | 実機 | 内容 |
|---|---|---|
| `tests/unit/tinyusb_config` | 不要 | buffer サイズの `#ifndef` が効くこと、32768 超がビルドエラーになること |
| `tests/single/descriptor` | ESP32-S3 | MS OS 2.0 の flat / subsets、vendor の per-speed MPS、HID vendor の Report Count |
| `tests/single/p4_hs_packet_sizes` | ESP32-P4 | 511 byte report の descriptor、HS 512 / FS 64、FIFO 容量、observer の着脱 |
| `tests/loopback/usb_vendor` | ESP32-P4 | host 役が取りに行った MS OS 2.0 が 162 byte、observer が MS OS 2.0 要求と GET_DESCRIPTOR を観測 |
| `tests/loopback/` 全体 | ESP32-P4 | 既存 16 本の回帰 |
| `tests/peer/` `tests/single/` | ESP32-S3 ×2 | 36/37。失敗した 1 本（`peer/usb_msc`）は DUT が EspUsbHost 単体で、このライブラリを include していない起動時 OOM |
| `tests/manual/p4_hs_stream` | ESP32-P4 + PC | bulk IN の FIFO / 転送長 / waitWritable / host URB depth |
| `tests/manual/p4_hs_hid_stream` | ESP32-P4 + PC | HID 511 byte の descriptor と 4.03 MB/s |
| `tests/manual/windows_winusb` | ESP32-P4 + Windows | WinUSB bind と、旧構造での Code 28 対照実験 |
| `tools/build_check.py` | 不要 | esp32s3 / esp32s2 / esp32p4 の全 example |

**借りたボードについて。** `esp32-p4-30eda0e31478` には、いま
`tests/manual/windows_winusb` のファーム（VID/PID `303a:4043`、serial `espusb-winusb-flat-2`）が
入っている。E069〜E078 の sketch は上書きされているので、必要なら `wch-protocols` 側から
書き戻してほしい。usbip の共有状態は、OTG（`1209:0008` の instance）を共有したままにしてある。
