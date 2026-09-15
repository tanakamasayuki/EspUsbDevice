# 改修依頼への回答

依頼元: [ch32-riscv-ug/wch-protocols](https://github.com/ch32-riscv-ug/wch-protocols)。
ESP32-P4 の USB 2.0 HS を実測しているプロジェクトで、そこで出た実測値をもとに
このライブラリへの改修依頼が起票される。

このドキュメントは回答を回ごとにまとめたものである。

| 回 | 依頼元ドキュメント | 内容 | 状態 |
|---|---|---|---|
| 1 | [`references/espusbdevice-change-requests.ja.md`](https://github.com/ch32-riscv-ug/wch-protocols/blob/main/references/espusbdevice-change-requests.ja.md)（2026-09-12 版） | CR-1〜CR-9。E069〜E078 から起票 | **9 件すべて対応・実機確認済み** |
| 2 | [`references/usb-library-feedback.ja.md`](https://github.com/ch32-riscv-ug/wch-protocols/blob/main/references/usb-library-feedback.ja.md)（2026-09-15 版） | 既定値 D1〜D4、機能 F1〜F5 | D1〜D4・F4・F5 は[こちらで実測して処理](#第-2-回-d1d4--f1f5)。F1〜F3 は[設計まで](#f1f3-non-buffered-経路)、実装判断待ち |

**他所の実測を根拠にしない**、というのがこの往復のやり方である。依頼元の数字は
「測る価値がある」の根拠として扱い、採否は必ずこちら側で測り直してから決めている。

---

## 第 1 回 CR-1〜CR-9

対象: [`references/espusbdevice-change-requests.ja.md`](https://github.com/ch32-riscv-ug/wch-protocols/blob/main/references/espusbdevice-change-requests.ja.md)
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

**依頼元 E078 で、これが host 側から使える指標になった。** 短く返る URB には 2 種類あり、
向きが逆である。

- **半端な長さの転送**: `write()` は FIFO の空きぶんしか受け取らないので、生産側が
  追いついていると 4032 byte のような半端が積まれ、packet size の倍数でない転送になる。
  `waitWritable(4096)` → `write(4096 ちょうど)` に変えると消える（依頼元実測: 84 MHz で
  183/296 本 → 88 MHz 以上で 0 本）。
- **ZLP**: FIFO が枯れたときに出る。**生産側が追いついていない**ことの印である。

つまり半端を潰したあとに残る短い URB は「device がバスではなくデータを待っている」ことを
意味し、弾性 FIFO の占有が伸びるかどうかとは逆向きに動く。**2 つ揃えて見ると 1 本の run で
釣り合い点のどちら側にいるか判定できる**、というのが依頼元の使い方である。

なおこちらの `p4_hs_stream` が FIFO 4096 以上で常に 0 本なのは、数え方の違いではなく
**生産側がいないから**である。手元にデータが揃っているので毎回 FIFO 容量ぶんちょうどが
書き込まれ（`write()` が空きぶんに clamp する ＝ 容量より多く差し出せば必ず容量ぶん入る）、
どの転送も 4096 = 8 packet になる。しかも空きができた瞬間に埋め直すので、転送完了時に
`tu_edpt_stream_write_xfer()` が 0 を返さず、ZLP の分岐に入らない。

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
依頼書の「`stalls` が桁で減る」はこれで満たしている。

### 依頼元 E078 での確認（この API の値打ちは単体の MB/s には出ない）

依頼元が PARLIO の 2ch capture と同居させて測り直した結果が来た。こちらの単体測定では
見えない側で効いている。

- **`stalls` は全条件で 0、`waits` は転送数と一致**（16 MiB で 4,095、64 MiB で 16,383 =
  byte ÷ 4096）。1 転送につき 1 回だけ block という形が、こちらの 4 MiB / waits 1,022 と一致した。
- **capture と同居しても device 側の帯域が落ちない**（21.4〜22.4 MB/s。こちらの単体 21.1 と同等）。
  連続 streaming は **86 Msps まで継ぎ目なし**で、88 Msps から弾性 FIFO の占有が時間に比例して伸びる。
- **[E067] の「capture と USB を同時に走らせると USB が 7〜16% 落ちる」が再現しなくなった。**
  あれはバス競合ではなく、**送出 task の spin が harvest から CPU を奪っていた**というのが結論。
  依頼書が CR-9 の効き所として挙げていたのがまさにここで、単体の帯域比較（21.12 対 20.94）には
  出ない差である。

依頼書の「`stalls` が桁で減り、harvest 側の余裕が下がる」という合格条件は、これで満たされた。

---

## 作業中に見つかった別件（修正済み）

依頼書の範囲外だが、複合 HID の経路に 2 件あった。どちらも直した。

### 1. report descriptor の merge が item 境界を無視していた

`EspUsbDevice::buildDescriptors()` の複合 HID merge は、各クラスの report descriptor の
**先頭 6 byte を prologue と決め打ち**して、その後ろに Report ID item を差し込んでいた。
keyboard / mouse は `05 xx 09 xx a1 01` でちょうど 6 byte だが、vendor HID の usage page は
3 byte item（`06 00 ff`）なので prologue は 7 byte ある。6 byte で切ると **Collection item の
途中**で切ることになり、そこから先の item が 1 byte ずつずれる。ホストは `a1 85` を
「Collection (vendor 定義)」と読み、以降すべて別物になる。デバイスは列挙するので気付かない。

Collection (Application) item の終わりを**item 単位で走査して**見つけるようにし、そこに
Report ID を挿す形に直した。自前で Report ID を持つクラス（gamepad / consumer control /
system control / vendor HID）は、重複させずに置き換える（従来は `85 04 85 04` のように
同じ item が 2 回出ていた。値が同じなので無害だったが、無駄だった）。

### 2. 複合 HID が interrupt OUT の report を全部捨てていた

TinyUSB の HID driver は report descriptor を解釈しないので、interrupt OUT に届いた report を
`tud_hid_set_report_cb(instance, 0, ...)` と **report ID 0** で上げてくる
（`class/hid/hid_device.c`。control SET_REPORT の経路だけは wValue から ID を取り出す）。
一方、複合 HID の振り分けは report ID で行う。結果、**複合デバイスは interrupt OUT に来た
report をひとつも受け取れなかった**。単独クラスの device は `instance` で振り分けるので無事。

merged descriptor は report ID を宣言しているので、仕様上ホストは report の先頭バイトに
ID を置く。そこから読むようにした。先頭バイトがどのクラスの ID でもないときは何もしない
（ID を宣言していない構成で payload の 1 byte 目を食べないため）。

### 確認の取り方（と、取れなかったこと）

- merge は `tests/single/descriptor` が item 単位で走査して検証する（well-formed、
  Collection (Application) の数、Report ID の並び、Collection より前に Report ID が無いこと）。
- 実ホストから見た merged descriptor と両 ID の往復は
  `tests/loopback/composite_hid_report_ids` が P4 実機で確認する。ホストが取得した
  descriptor をホスト側で走査し、ID 1 と ID 6 の IN、control 経由の feature (ID 6) と
  LED (ID 1) を通す。
- **interrupt OUT の経路だけは、この台のどちらのホストからも駆動できなかった。** usbip は
  interrupt OUT URB を配送せず（control SET_REPORT は通る。`hidraw` の ioctl で確認）、
  EspUsbHost には生のエンドポイント書き込み API が無く、`sendHIDVendorOutput()` は複合
  デバイスには届かない（device 側の `handleHidSetReport` が一度も呼ばれない）。そのため
  `tests/single/descriptor` から `handleHidSetReport()` を直接呼んで振り分けだけを検証して
  いる。ホストが ID を前置するかどうかはホストの責任で、device の責任は振り分けである。

---

## 確認したこと

| 対象 | 実機 | 内容 |
|---|---|---|
| `tests/unit/tinyusb_config` | 不要 | buffer サイズの `#ifndef` が効くこと、32768 超がビルドエラーになること |
| `tests/single/descriptor` | ESP32-S3 | MS OS 2.0 の flat / subsets、vendor の per-speed MPS、HID vendor の Report Count |
| `tests/single/p4_hs_packet_sizes` | ESP32-P4 | 511 byte report の descriptor、HS 512 / FS 64、FIFO 容量、observer の着脱 |
| `tests/loopback/usb_vendor` | ESP32-P4 | host 役が取りに行った MS OS 2.0 が 162 byte、observer が MS OS 2.0 要求と GET_DESCRIPTOR を観測 |
| `tests/loopback/composite_hid_report_ids` | ESP32-P4 | 実ホストが取得した merged descriptor と、report ID 1 / 6 の双方向 |
| `tests/loopback/` `tests/single/` | ESP32-P4 | 新既定で全数 |
| `tests/peer/` | ESP32-S3 ×2 | 全数。一度 `peer/usb_msc` が起動時 OOM で落ちたが、`--clean` で通る（README にある stale build cache の症状で、DUT 側は EspUsbHost 単体でこのライブラリを include していない） |
| `tests/manual/p4_hs_stream` | ESP32-P4 + PC | bulk IN の FIFO / 転送長 / waitWritable / host URB depth |
| `tests/manual/p4_hs_hid_stream` | ESP32-P4 + PC | HID 511 byte の descriptor と 4.03 MB/s |
| `tests/manual/windows_winusb` | ESP32-P4 + Windows | WinUSB bind と、旧構造での Code 28 対照実験 |
| `tools/build_check.py` | 不要 | esp32s3 / esp32s2 / esp32p4 の全 example |

最終確認は `uv run --env-file .env pytest --clean` で **71 件すべてパス**（1:08:14）。
83 本の DUT / peer ログに予期しない不審行は 0、既知の許容が 4 件（MSC の GET_MAX_LUN STALL、
peer 再書き込み中の Enqueue URB error）。example ビルドは esp32p4 28 / esp32s3 29 /
esp32s2 26 で失敗 0、unit 17 パス。

### 自動テストで押さえていないもの

**`waitWritable()` の起床順序の修正だけは、手動ハーネスでしか検出できない。**
症状は「帯域が落ちる」であって「動かない」ではないので、検出するには
「1 回の待ちが転送時間ではなく timeout の刻みで終わっている」ことを時間で見るしかない。
loopback は full speed で、1 転送 4096 byte が約 3.4 ms、待ちの刻みが 2 ms なので、
**同じバグが再発しても時間では分離できない**。high speed で 1 転送が約 195 us になって初めて
刻みとの差が 10 倍になり、そこで初めて見える（実測 1.85 対 20.94 MB/s）。
そのため `tests/manual/p4_hs_stream` を実行手順ごと残してある。
`stalls=0 waits=1022` が出れば正常、`waits` がほぼ同じで帯域が桁で落ちていれば再発である。

**借りたボードについて。** `esp32-p4-30eda0e31478` には、いま
`tests/manual/windows_winusb` のファーム（VID/PID `303a:4043`、serial `espusb-winusb-flat-2`）が
入っている。E069〜E078 の sketch は上書きされているので、必要なら `wch-protocols` 側から
書き戻してほしい。usbip の共有状態は、OTG（`1209:0008` の instance）を共有したままにしてある。

---

## 第 2 回 D1〜D4 / F1〜F5

対象: [`references/usb-library-feedback.ja.md`](https://github.com/ch32-riscv-ug/wch-protocols/blob/main/references/usb-library-feedback.ja.md)
（2026-09-15 版、E107〜E114 から起票）。既定値を変えるべき 4 件（D1〜D4）と機能追加
5 件（F1〜F5）。

**依頼元の数字は一切そのまま採らず、全件こちらで測り直した。** その結果、**D1 は採用、
D2 は API だけ公開して既定は据え置き**という別々の結論になっている。同じ依頼書の
同じ根拠でも、負荷の形が違えば答えが変わるためである。

### 一覧

| | 内容 | 依頼元の実測 | こちらの実測 | 結果 |
|---|---|---|---|---|
| [D1](#d1-bulk-in-の送信-fifo-を-2-packet-に) | bulk IN の送信 FIFO を 2 packet に | 29.7 → 49.3 MB/s | **22.98 → 28.93 MB/s** | **採用**。既定で自動有効 |
| [D2](#d2-usbd-task-の-core-固定) | usbd task の core 固定 | 44 → 52 Msps | **28.61 → 28.90 MB/s（差なし）** | **API のみ公開、既定は据え置き** |
| [D3](#d3-buffered-write-の端数が-flush-まで出ない) | buffered write の端数が出ない | — | 再現・原因特定 | **修正** |
| [D4](#d4-dma-mode-が既定であることの明記) | DMA mode 既定の明記 | — | — | **文書化** |
| [F1〜F3](#f1f3-non-buffered-経路) | zero-copy TX / TX 完了 callback / direct RX callback | 209 → 247 Mbps（D1 併用 389〜395） | 未測 | **保留**。同梱 TinyUSB への patch が要るので、判断材料は速度ではなく vendoring の不変条件 |
| [F4](#f4-tud_configure-の露出) | `tud_configure()` の露出 | — | — | **D1 の実装で内部的にカバー**。生の構造体は非公開 |
| [F5](#f5-転送長の-32-bit-化) | 転送長の 32-bit 化 | — | — | **見送り**（upstream 依存） |

測定条件（D1・D2 共通）: ESP32-P4 HS、usbip 経由、一方向 device→host bulk IN、
1 run 32 MiB を 3 回の最良、`waitWritable(writeCapacity())` + `write()` の buffered 経路、
vendor FIFO は既定の 4096/4096。host は pyusb の同期 read。

### D1 bulk IN の送信 FIFO を 2 packet に

DWC2 は IN endpoint に既定で 1 packet 分の FIFO しか割り当てないので、いま送っている
packet がコントローラから出るまで次を用意できない。`tud_configure()` の
`bm_double_buffered` に該当 endpoint の bit を立てると 2 倍になる。

```
1 packet:  22.98 / 22.61 / 21.63 MB/s
2 packet:  28.74 / 28.86 / 28.93 MB/s
```

**+26%。** 依頼元の +66% より伸びが小さいのは、依頼元の firmware が zero-copy 側
（F1）で copy 律速を外しているためと理解している。つまりこの数字が、**buffered 既定の
ライブラリ構成で D1 単独が持つ効果**である。回ごとのばらつきも ±0.7 → ±0.1 MB/s に縮む。

**採用したが、無条件には有効化していない。** 増えるのは RAM ではなく DFIFO という
コントローラ内の固定資源で、足りないと `dcd_edpt_open()` が false を返して endpoint が
開かない。遅いデバイスではなく**列挙に失敗するデバイス**になる。そのため configuration
descriptor から DWC2 port と同じ式で収支を計算し、bulk IN が全部収まるときだけ立てる。

```
available = fifo_depth - 2 * endpoint_count
receive   = 14 + 2 * (largest_out_packet / 4 + 1) + 2 * endpoint_count
needed    = ceil(64/4) + IN endpoint ごとの ceil(packet/4) の総和
          + bulk IN endpoint ごとの ceil(packet/4) の総和
```

依頼元の「P4 HS は RX 304 + EP0 16 + EPInfo 32 を除いて 672 words 空き」と一致する。
結果として **S2/S3 は bulk IN 4 本でも必ず収まり、P4 HS は 2 本まで収まって 3 本は収まらない**。

API は `config.bulkInBuffering`（`Auto` / `Single` / `Double`）と
`EspUsbDevice::bulkInDoubleBuffered()`。`Auto` が既定で、全部か無しかにしてある——
一部だけ 2 packet にする規則だと、スループットが function の登録順に依存してしまう。
`Double` は要求で、収まらなければ `begin()` が `ESP_ERR_INVALID_SIZE` で失敗する。
テストは [`tests/single/bulk_in_fifo`](../tests/single/bulk_in_fifo)。

### D2 usbd task の core 固定

依頼元は E107 で、取り込み pipeline が 44 → 52 Msps になったと実測している。

こちらでも測った。**差が出なかった。**

```
pin なし（既定）:   28.61 / 28.51 / 28.59 MB/s
core 0 固定:        28.71 / 28.82 / 28.90 MB/s
```

**負荷の形が違うからである。** こちらのハーネスは producer が「静的パターンを `loop()` から
memcpy するだけ」で、usbd task と取り合う相手がいない。依頼元の E107 は PARLIO capture と
codec という本物の producer が反対の core にいるので、分離する意味がある。

つまり D2 は「**producer が重いときに効く**」という条件付きの話であり、ライブラリの
既定にするものではない。既定を pin ありにすると、producer が軽い構成ではスケジューラの
自由を奪うだけになる。

`config.taskCoreId` として公開し、**既定は -1（固定しない＝従来どおり）**のままとした。
どういうときに手を伸ばす価値があるかは
[応用ガイド 5.6](usb-device-advanced.ja.md#56-usbd-taskをどのcoreで走らせるか) に、
両方の実測値を添えて書いてある。

なお 1 回目の測定で 1 run だけ 16.38 MB/s が出たが、2 回目では再現しなかった。
usbip 経路にまれに出る穴と見て、pin の効果としては記録していない。

### D3 buffered write の端数が flush まで出ない

**実在した。** `tu_edpt_stream_write()` は FIFO が `wMaxPacketSize` 分たまってからでないと
転送を arm しない。

```c
if ((tu_fifo_count(&s->ff) >= s->mps) || (tu_fifo_depth(&s->ff) < s->mps)) {
  tu_edpt_stream_write_xfer(s);
}
```

結果として 2 つのことが起きる。

1. **mps 未満の書き込みは `flush()` まで送られない。** 16 byte の返信は FIFO に残る。
2. **`waitWritable(writeCapacity())` が永久に返らない。** 端数が FIFO にあり、それを
   押し出すものが無い。そして**空きを待って止まっている呼び出し側が、端数を切り上げる
   バイトを追加してくれることはない**。これが依頼書の言う「永久待ち」の正体である。

`waitWritable()` が**待つ前に flush する**ようにした。呼び出し側が既にブロックしている
場面でだけ短い packet が 1 つ出る形なので、スループットへの影響は無い。

**`write()` 側に自動 flush は入れていない。** 短い packet はホストの実行中 URB を早期完了
させ、再投入の往復を生む（第 1 回 CR-5 で測ったとおり）。小さく何度も書くスケッチで
これをやると、静かに遅くなる。代わりに「短い message には `flush()` が要る」ことを
[応用ガイド 6.3](usb-device-advanced.ja.md#63-実測スループット) に明記した。

### D4 DMA mode が既定であることの明記

依頼元が slave mode 前提で 2 実験ぶん誤解した、という報告。応用ガイド 2.3 の冒頭が
「2 つの転送モードがあり、このライブラリは DMA を使います」で、**選べるように読めた**のが
原因だったと見ている。

「**対応する全ターゲットで DMA モードをビルドし、slave はサポート構成でもビルドの
選択肢でもない**」と明記した。あわせて、依頼元がその後に特定した
「**host が libusb の完了 callback 内で処理をすると、device 側の不具合に見える停止が
起きる**」も応用ガイド 6.3 に入れてある。こちらは device を疑う前に見る場所の話なので、
測定ホストを書く人に効く。

### F1〜F3 non-buffered 経路

- **F1** zero-copy TX: `CFG_TUD_VENDOR_TXRX_BUFFERED=0` のとき、呼び出し側 buffer を
  そのまま `usbd_edpt_xfer()` へ渡す。完了まで所有権は呼び出し側。依頼元実測 209 → 247 Mbps、
  送出 core の task 負荷 57〜66% → 7%
- **F2** TX 完了 callback: 完了 byte 数を渡す hook（usbd task context、callback 内から次の
  buffer を投入できる）
- **F3** non-buffered 時の direct RX callback: 現在の `onRx(size)` は buffered 専用

#### まず読み違えた点（訂正）

最初、`CFG_TUD_VENDOR_TXRX_BUFFERED=0` を調べて「pin 版に zero-copy は無いので、依頼元の
209 → 247 Mbps は copy 1 回分の削減だろう」と書いた。**これは誤り。** 依頼元の F1 は
config の切り替えではなく、**同梱している TinyUSB の source に当てる patch** である
（E102 / E108、pin 版 v0.21.0 の同じ `vendor_device.c` に対する patch）。209 → 247 Mbps は
その memcpy 撤去込みの値であり、さらに D1（FIFO 2 packet）を足して 389〜395 Mbps。

以下は、その前提で書き直したもの。

#### stock の `CFG_TUD_VENDOR_TXRX_BUFFERED=0` では F1 にならない

pin 版（`53f8c53c`, v0.21.0）の non-buffered write は、呼び出し側 buffer を
`usbd_edpt_xfer()` へ渡すのではなく、class 自身の epbuf へ `memcpy` してから渡す
（[`src/class/vendor/vendor_device.c`](../src/class/vendor/vendor_device.c),
`vendord_ep_write()`）:

```c
const uint32_t xact_len = tu_min32(len, bufsize);
memcpy(epbuf, buffer, xact_len);
TU_ASSERT(usbd_edpt_xfer(p_itf->rhport, ep, epbuf, (uint16_t) xact_len, false), 0);
```

しかも 1 回の write が `CFG_TUD_VENDOR_TX_EPSIZE`（このライブラリの P4 既定で 4096、
S2/S3 は TinyUSB 既定の `TUD_EPSIZE_BULK_MAX` = full speed で **64**）に clamp され、
`write_available()` は endpoint が busy の間 0 を返す。**つまり stock の switch を
入れるだけなら、S2/S3 では 512 byte の FIFO を失って 64 byte 単位・転送中 1 本に
落ちる。** この switch 単体は採らない。

**E102 / E108 の patch はこの 2 つを両方外している。** memcpy を撤去して呼び出し側
buffer を直接渡し、clamp を `tu_min32(len, 0xffffu)`（`usbd_edpt_xfer()` の 16-bit 長
だけが上限）に広げる。E108/E110 では 27,136 byte を 1 transfer で流し、TX 完了
callback から次を arm して **49.3 MB/s（usbip）** を実測している。

#### したがって本当の判断材料は vendoring の不変条件

このリポジトリは TinyUSB を **byte-for-byte** で同梱し、
[`tools/verify_tinyusb_vendor.py`](../tools/verify_tinyusb_vendor.py) が upstream
`53f8c53c` との一致を検証する（48 files / 14 sources）。F1 を入れるとは、
**その不変条件を「upstream + 管理された patch 列」に変える**ということである。
`PROVENANCE.md` / `BUILD_FILES.txt` / verifier / `update_tinyusb_vendor.py` が
すべてこの前提に乗っているので、コストはここに集中する。速度ではない。

#### patch なしで同じ経路に乗れる（source で確認）

**結論から言うと、vendoring を崩さずに済む見込みが高い。** IN endpoint を vendor class に
任せず、ライブラリ自身が `usbd_edpt_claim()` + `usbd_edpt_xfer()` で呼び出し側 buffer を
直接投げ、完了を `tud_vendor_tx_cb()` で受ければ、E108 と同じ経路になる。

- `usbd_edpt_claim()` / `_xfer()` / `_release()` / `_busy()` は
  [`src/device/usbd_pvt.h`](../src/device/usbd_pvt.h) にあり、**このリポジトリでは
  すでに同梱済みかつ使用中**（[`src/EspUsbDeviceCcid.cpp`](../src/EspUsbDeviceCcid.cpp),
  [`src/internal/EspUsbDeviceAppDriver.h`](../src/internal/EspUsbDeviceAppDriver.h),
  `third_party/tinyusb/BUILD_FILES.txt`）。
- `EspUsbDeviceAppDriver.h` の冒頭が「同梱 TinyUSB は upstream-verbatim なので、
  TinyUSB が実装していない class はここに登録する」と書いている。**不変条件を守ったまま
  拡張する仕組みが既にある**ということで、この道はその延長になる。
- `usbd_edpt_xfer()` の長さは `uint16_t` なので 65,535 まで。`TX_EPSIZE` の clamp は
  そもそも `vendord_ep_write()` の中の話なので、通らなければ関係ない。

**buffered のままだと、direct transfer の完了後に class が ZLP を arm する。**
`vendord_xfer_cb()` の IN 分岐は `tud_vendor_tx_cb()` のあとに
`tu_edpt_stream_write_xfer()` → 0 なら `tu_edpt_stream_write_zlp_if_needed()` と続き、
後者は [`src/tusb.c`](../src/tusb.c) 374 行で

```c
TU_VERIFY(tu_fifo_empty(&s->ff) && last_xferred_bytes > 0 && (0 == (last_xferred_bytes & (s->mps - 1))));
TU_VERIFY(stream_claim(s));
TU_ASSERT(stream_xfer(s, 0));
```

を満たす（27,136 & 511 == 0）。この分岐は `#if CFG_TUD_VENDOR_TXRX_BUFFERED` の中にあり、
non-buffered の `#else` には無い。`stream_claim()` は `usbd_edpt_claim()` を呼ぶだけで
app 側と同じ mutex、[`src/device/usbd.c`](../src/device/usbd.c) 747 行が busy と claimed を
`xfer_cb` の**前**に落とすので、callback の中なら app が先に claim を取れる。

#### 実測（ESP32-P4 high speed、mps 512）

ライブラリに試作（`writeDirect()` ＋ `onTxComplete()`、TinyUSB は無改変）を入れ、native USB を
PC に出した P4 で測った。`shorts` はホスト側の 1 ブロック未満の読み出し、`zerolen` は
デバイス側で `onTxComplete()` が 0 byte を報告した回数。corrupt と gap は全構成で 0。

| build | 次を arm する場所 | stage | blocks | shorts | zerolen | MB/s |
|---|---|---|---|---|---|---|
| buffered | 完了 callback | 27,136 | 5757 | 0 | 0 | 24.43 |
| buffered | **別タスク** | 27,136 | **1** | **1** | **1** | **停止**（`armfail=1`） |
| non-buffered | 完了 callback | 27,136 | 6161 | 0 | 0 | **27.86** |
| non-buffered | 別タスク | 27,136 | 5921 | 0 | 0 | 26.78 |
| buffered | 完了 callback | 8,192 | 12218 | 0 | 0 | 16.68 |
| buffered、**stock の `write()` 512 B**（対照） | 別タスク | 512 | 20200 | **8183** | **8183** | 1.04 |

読み出しは pyusb の同期読み（URB 1 本ずつ）なので、**絶対値は host 律速**であり
依頼元の 1 MiB URB × depth 8 の数字とは比較にならない。意味があるのは構成間の差である。

三つ読み取れる。

1. **ZLP は実在し、buffered ビルド固有である。** 対照行が決定的で、stock の 512 B
   write（ちょうど mps）ごとに ZLP が出て、host の `shorts=8183` と device の
   `zerolen=8183` が一致する。non-buffered では arm 位置によらず 0 である。
2. **buffered ＋ direct write は、完了 callback の中で arm しないと止まる。**
   レイテンシが乗るのではなく停止する。ZLP が claim を握り、別タスクの
   `writeDirect()` が `armfail` で弾かれ、こちらの試作には再試行が無いので
   そこで終わる。
3. **non-buffered のほうが速く、arm 位置に鈍い。** 同じ完了 callback arm で
   27.86 対 24.43 MB/s（+14%）、別タスク arm でも 26.78 MB/s で完走する。

#### 途中で 2 回間違えたので、経緯を残す

最初に source からこの ZLP を予測し、次に ESP32-S3 full speed の実測で「出ない」として
予測を撤回し、P4 で再び出た。S3 の測定は無効だった。`build_opt.h` を変えたのに
`arduino-cli` に `--clean` を付けておらず、**ライブラリのオブジェクトが前回のビルドのまま**
使われていたためである。build_opt.h は応答ファイル経由でスケッチには効くので、
スケッチが `buffered=0` と出力しながらライブラリは buffered、という状態になる。
見分け方はビルドサイズで、設定を変えたのに `Sketch uses` が動かなければ効いていない
（P4 で 385,586 → 385,104 byte）。S3 の表は撤回した。

S3 full speed では、buffered ビルドの stock write（64 B ＝ mps）でも ZLP が出なかった。
P4 との差は未解明のまま残っている。ここで効くのは P4 のほうなので、追っていない。

#### buffer 側の契約（依頼元の運用）

- 64 byte 整列、整列長
- internal RAM（DMA 可）。PSRAM からは直接 DMA せず internal の bounce buffer 経由
- 書いた core で `esp_cache_msync(C2M)` してから渡す
- 完了 callback まで所有権は呼び出し側

API にするなら、これを契約として書き、**満たせない呼び出しは従来の epbuf copy 経路へ
落とす**のが妥当、というのが依頼元の提案。既定安全でオプトインの速い道になるので、
この形なら公開 API として筋が通る。

#### F2 は mode の切り替えなしに今日出せる

`tud_vendor_tx_cb(idx, sent_bytes)` は **buffered / non-buffered の両方で呼ばれる**
（`vendord_xfer_cb()` の両分岐に入っている）。このライブラリは既にこれを
`EspUsbDeviceVendor::handleTxComplete()` で受けて `waitWritable()` を起こしている。
公開するのに mode の切り替えも所有権の規約も要らない。

ただし「出せる」と「出すべき」は別である。`waitWritable()` は、この callback を
**タスクを起こす**ために使う API として既にある。生の callback を公開すると、次の block を
usbd task の context から積む書き方になり、応用ガイド 6.3 が警告している
「host 側が完了 callback 内で処理をして device の不具合に見える停止を起こす」の
device 版を作れてしまう。ただし F1 を入れるなら、完了駆動で次を arm するのは
**必須**であって選択肢ではない（E108/E110 がそうしている）。

#### したがって設計は 2 択

| | 形 | 得 | 損 |
|---|---|---|---|
| **A** | direct write は `CFG_TUD_VENDOR_TXRX_BUFFERED=0` を要求（build 時条件なので API で弾ける） | ZLP 論理が compile されないので arm 位置に鈍く、実測でも速い（27.86 対 24.43 MB/s）。F3（direct RX callback）も同時に手に入る | `EspUsbDeviceVendor` の `available()` / `read()` / `flush()` が build flag で消える。S2/S3 で有効にすると 64 byte clamp に落ちる。テストが 2 構成になる |
| **B** | vendor interface を [`EspUsbDeviceAppDriver`](../src/internal/EspUsbDeviceAppDriver.h) 側に持ち、IN endpoint の `xfer_cb` を自前にする | `vendord_xfer_cb` の ZLP 論理からも `tud_vendor_tx_cb` の束縛からも自由。buffered の既存 API をそのまま残せる。S2/S3 も従来どおり | class driver を 1 つ自前で持つ（CCID の前例あり）。descriptor / MS OS 2.0 / WebUSB / control request / alt setting と既存 vendor 機能の同居が設計論点 |

**実測を見たうえで A を推す。** buffered ＋ direct は「arm は完了 callback の中でだけ」を
守れば動くが、守らないと**停止する**。公開 API がその契約に全体重を預ける形は、
`waitWritable()` のような既存の使い方と混ぜたときに壊れる。A なら ZLP 論理自体が
存在しないので、契約は「速いほうの buffer 条件」だけで済む。

B は残す価値がある。A で消える `available()` / `read()` / `flush()` を取り戻せるのは B
だけで、S2/S3 の 64 byte clamp も避けられる。ただし class driver を 1 つ背負うので、
A で数字と使い勝手を確かめてからでよい。

#### 正規実装の実測（決着）

公開 API を実装したうえで測り直した。送るデータは事前に用意し、host は同じもので、
「読み」は host が同時に投げている bulk read の本数である。

| 経路 | stage | 読み 1 本 | 読み 8 本 |
|---|---|---|---|
| buffered `write()` ＋ `waitWritable()` | — | 21.8 | 33.9 |
| direct `writeDirect()` | 8,192 | — | 39.7 |
| direct `writeDirect()` | 27,136 | 27.4 | 41.1 |
| direct `writeDirect()` | 65,024 | 30.4 | **42.6** |

条件を揃えて **+26%**（65,024）と +24%（27,136）。

**依頼元のハーネス（1 MiB URB × 深さ 8）では 46.6〜48.3 MB/s** で、**独自に TinyUSB へ
patch を当てた E110 の 49.0〜49.3 と数 % 差**だった。**同梱 TinyUSB を無改変のまま、
patch 版と同じ天井に届いている。** これで F1 の目的は達成である。

途中で「device が 23 MB/s で律速している」という疑いが出たが、原因は 2 つとも測定側
だった。(1) こちらの計測スケッチが stage ごとに 27 KB を usbd task 上で埋めていた
（事前計算にして 22.8 → 27.4 MB/s）。(2) こちらの reader が URB を 1 本ずつ同期で
読んでいた（8 本 in flight にして 30.4 → 42.6 MB/s）。依頼元から出た 2 つの仮説
——slave mode ではないか、FIFO が 1 packet のままではないか——はレジスタの実測で
どちらも否定された。

```
gahbcfg=0x00000027 dmaen=1 gintmsk=0x80003004 rxflvl=0
ghwcfg2_arch=2 dieptxf1=0x02000600 → depth 512 words（512 byte packet 4 個分）
```

`gahbcfg` の値は依頼元が 2.3.0 release で記録したものと同一である。

#### 結論（現時点）

| | 判断 | 根拠 |
|---|---|---|
| F1 | **試作が動いた。公開 API は A で実装する** | patch 不要を実証（`usbd_edpt_claim()` + `usbd_edpt_xfer()` はすでに同梱・使用中で、`EspUsbDeviceAppDriver` が同じ不変条件のもとで先例になっている）。P4 HS 実測で non-buffered 27.86 / buffered 24.43 MB/s |
| F2 | **F1 の前提条件**（付属品ではない） | buffered では完了 callback の中で arm しないと ZLP に claim を取られて停止する。non-buffered でも、完了駆動でなければ転送が繋がらない |
| F3 | A を採れば同時に入る | non-buffered の `tud_vendor_rx_cb()` は buffer を直接渡す。S2/S3 で有効にすると 64 byte clamp に落ちる点は変わらないので、既定は buffered のまま |

試作の位置: `EspUsbDeviceVendor::writeDirect()` と `onTxComplete()` を working tree に
入れてある。同梱 TinyUSB は byte-for-byte のまま。自前リグの
`tests/peer/usb_vendor_direct`（一時）で end-to-end も通している。**正規実装では
これを A の形に整え、フル回帰を通してからリリースする。**

### F4 `tud_configure()` の露出

D1 の実装で内部的に呼んでいる（`tusb_init()` の前、計算した `bm_double_buffered` と、
既定を保った `vbus_sensing`）。**生の構造体は公開していない。** 公開すると、収支を無視した
bitmap を渡して列挙に失敗する構成が作れてしまう。`config.bulkInBuffering` が、その計算を
挟んだ形の公開 API にあたる。

### F5 転送長の 32-bit 化

TinyUSB 側が `uint16_t` なので upstream 依存。**見送り。**
