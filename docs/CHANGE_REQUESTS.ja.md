# 改修依頼への回答

対象: `wch-protocols` の
[`references/espusbdevice-change-requests.ja.md`](https://github.com/tanakamasayuki/EspUsbDevice)
(2026-09-12 版、ESP32-P4 の USB 2.0 HS 実測 E069〜E078 から起票された CR-1〜CR-9)。

依頼側の測定環境は ESP32-P4 rev 1.3 2 枚、Arduino-ESP32 3.3.11、host は WSL + usbip + libusb。
このリポジトリ側で再現・検証できたものと、依頼側の実験台でしか確かめられないものを分けて書く。

## 一覧

| | 内容 | 対応 |
|---|---|---|
| [CR-1](#cr-1-ms-os-20-の-subset-構造) | MS OS 2.0 の subset 構造 | **実装** — interface 数で自動判定 + `config.msOs20Layout` |
| [CR-2](#cr-2-control-request-の観測-hook) | control request の観測 hook | **実装** — `EspUsbDevice::onAnyControlRequest()` |
| [CR-3](#cr-3-per-speed-の-endpointsize) | per-speed の `endpointSize` | **実装** — FS 側が 64 になる |
| [CR-4](#cr-4-vendor-の-fifo-深さ) | vendor の FIFO 深さ | **実装** — 全 buffer に `#ifndef`、P4 既定 8 KiB |
| [CR-5](#cr-5-帯域のばらつき) | 帯域のばらつき | **原因の見立て** — core 内蔵は slave mode、こちらは DMA mode |
| [CR-6](#cr-6-arduino-cli-と-symlink) | arduino-cli と symlink | **文書化** — troubleshooting 4 章 |
| [CR-7](#cr-7-転送を-2-つ以上-in-flight-に) | 転送を 2 つ以上 in-flight に | **見立て** — TinyUSB を触らずに試せる。先にそれを測ってほしい |
| [CR-8](#cr-8-hid-の-packet-size) | HID の packet size 64 B 固定 | **実装** — P4 で 511 B report / 512 B endpoint |
| [CR-9](#cr-9-fifo-空き待ちの-api) | FIFO 空き待ちの API | **実装** — `waitWritable()` / `writeAvailable()` |

---

## CR-1 MS OS 2.0 の subset 構造

**仮説は正しい。** MS OS 2.0 の configuration subset / function subset を解決するのは
**usbccgp.sys** で、Windows がそれを読み込むのは composite device のときだけである。
単一 interface の device に function subset を付けると、compatible ID は結び付く先を持たず、
`USB\MS_COMP_WINUSB` が device node に届かない。**`setupapi.dev.log` に 1 行も書かれない**
というのがまさにその症状で、install が始まっていないので記録すべきものが無い。
device 側が 178 byte を正しく返していたのも仮説どおりで、**バイト列ではなく構造の問題**だった。

依頼の 3 案のうち「interface が 1 本だけのときは自動的に subset を省く」を既定にし、
明示指定も残した。

```cpp
EspUsbDeviceConfig config;
config.webusbEnabled = true;
config.msOs20Layout = ESP_USB_DEVICE_MS_OS_20_AUTO;     // 既定
// ESP_USB_DEVICE_MS_OS_20_FLAT     … 常に set header 直下(162 byte)
// ESP_USB_DEVICE_MS_OS_20_SUBSETS  … 常に subset 入り(178 byte)
```

`AUTO` は `bNumInterfaces` で決める。1 本なら flat(162 byte)、2 本以上なら subsets(178 byte)。
組み上がった結果は `EspUsbDevice::microsoftOs20UsesSubsets()` が返す。

**確認の取り方.** vendor 1 本だけの device なら
`bmRequestType=0xC0, bRequest=0x02, wIndex=7` に **162 byte** が返り、`WINUSB` は
offset 30 ではなく **offset 14** にある。Windows 側は依頼書のとおり Code 28 が消えること。

こちらでは P4 の loopback(`tests/loopback/usb_vendor`)で host 役に実際に取りに行かせて
162 byte を確認済み。**Windows での確認はそちらの実験台でお願いしたい。**

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

`responseLength` は**ライブラリがスタックへ渡したバイト数**(`wLength` で切り詰められる前)。
descriptor、WebUSB URL、MS OS 2.0 set のときだけ非ゼロになる。

**どこで拾っているか。** 同梱の TinyUSB は upstream と byte 単位で一致していることを
`tools/verify_tinyusb_vendor.py` が検査しているので、`device/usbd.c` には手を入れていない。
代わりに 2 か所ある。

- **vendor request は `tud_vendor_control_xfer_cb` の SETUP。** ライブラリが WebUSB /
  MS OS 2.0 を処理する前に通る。**STALL した要求も見える**のはこちらだけで、
  「来たが断った」と「そもそも来ていない」を区別できるようにするため、意図して ACK ではなく
  SETUP で報告している。
- **それ以外は `dcd_edpt0_status_complete` の ACK。** `usbd.c` が weak で置いていて
  dwc2 driver が使っていないフックで、標準要求(SET_ADDRESS / GET_DESCRIPTOR /
  SET_CONFIGURATION)や class 要求はここに来る。status stage が完了した要求だけなので、
  **STALL した標準要求は出ない**——これが唯一の穴で、usbd.c を触らずに塞ぐ方法は見つからなかった。

CR-1 の切り分けに使うなら、探すのは `type=0xC0 req=<bMS_VendorCode> idx=0x0007` の SETUP 行。
出れば descriptor 構造の問題、出なければ Windows が投げていない、で確定する。

callback は usbd タスク上で走るので、中では counter かキュー止まりにしてほしい。

---

## CR-3 per-speed の `endpointSize`

指摘のとおり `(void)endpointSize;` で捨てていた。基底が渡してくる per-speed の値を
上限として使うようにした。

- FS 側 configuration / OTHER_SPEED_CONFIGURATION: `min(constructor 値, 64)`
- HS 側: 従来どおり 512

constructor に 64 未満を渡した場合(`EspUsbDeviceVendor v(device, 32)`)はそのまま 32 になる。
per-speed 値は**上限であって置き換えではない**。

---

## CR-4 vendor の FIFO 深さ

`src/internal/EspUsbTinyUsbConfig.h` の **class buffer を全部 `#ifndef` で囲った**。
vendor TX/RX だけでなく CDC / MIDI / MSC / HID も同じ。sketch 側の `build_opt.h` から

```
-DCFG_TUD_VENDOR_TX_BUFSIZE=16384
```

で上書きできる(`build_opt.h` は `.ino` と同じ場所に置き、変更したら `--clean`)。

**P4 の既定を 8 KiB に上げた。** 測定値(9.03 → 10.59 MB/s、ばらつき ±19% → ±2.5%)を
そのまま採った。16/32 KiB が無意味という結果も一致するので 8 KiB で止めている。
S2/S3 は 512 のまま——FS bulk は 64 byte packet で 1.5 MB/s が上限なので、
8 packet 分あればバスの方が先に尽きる。

**64 KiB が壊れた理由が分かった。** `tu_edpt_stream_init()` は FIFO サイズを **`uint16_t`** で
受け取る(`src/common/tusb_private.h`)。65536 は 0 に切り詰められ、depth 0 の FIFO になる。
`usb_ready=1` だが `mounted=0` という症状と一致する。さらに `tu_fifo` は読み書き index を
`[0, 2*depth)` で回すので、上限は 32768 である。**32768 を超える指定はビルドエラーにした。**

P4 で既定を上げた代償は、HID 側(CR-8)と合わせて **RAM 9,024 byte**
(`examples/USBVendor` の実測: 71,204 → 80,228 byte)。vendor を使わない sketch も払う。
TinyUSB が buffer を静的に確保するためで、これは避けられない。

---

## CR-5 帯域のばらつき

**心当たりがある。core 内蔵 stack とこのライブラリは DWC2 の転送モードが違う。**

- core 内蔵: `CFG_TUD_DWC2_DMA_ENABLE` をどこでも定義していない(precompiled libs の
  `tusb_config.h` にも `sdkconfig` にも無い)。TinyUSB 既定は 0 なので
  `CFG_TUD_DWC2_SLAVE_ENABLE = !0 = 1`、つまり **slave mode**。
- `EspUsbDevice`: `CFG_TUD_DWC2_DMA_ENABLE 1` / `CFG_TUD_DWC2_SLAVE_ENABLE 0` で
  **DMA mode**。2.1.0 で意図して切り替えた。slave mode の FIFO 再充填経路が
  CDC-NCM の device→host を数秒で恒久停止させたためで、戻す選択肢は無い。

この違いが「速さは互角、ばらつきだけ違う」という形と噛み合う。

- **slave mode** は FIFO-empty 割り込みから **ISR コンテキストで**次の packet を押し込む。
  転送の継ぎ目がタスクスケジューリングに依存しない。
- **DMA mode** は 1 転送完了 → 割り込み → event queue → usbd タスク起床 →
  `vendord_xfer_cb` → `tu_edpt_stream_write_xfer()` で再 arm、という**タスク往復**になる。
  この往復時間は、同じ優先度で回っている送出タスクの都合で変わる。

**ばらつきが `stalls` と逆相関する**という観測はこれで説明が付く。往復が遅れた回数がそのまま
FIFO が空だった回数であり、そのまま帯域の落ち込みである。FIFO を深くすると
**帯域より先にばらつきが縮んだ**(6.79–10.02 → 10.27–10.76)のも、深い FIFO が
スケジューリング揺らぎを吸収しているからと読める。

**もう 1 つ、ZLP が効いている可能性がある。** `tu_edpt_stream_write_zlp_if_needed()` は
「FIFO が空」かつ「直前の転送長が mps の倍数」のときに ZLP を送る(`src/tusb.c`)。
転送単位が 512 byte ちょうどである以上、**送出が一瞬でも途切れるたびに毎回条件が成立する**。
host 側の libusb bulk read は short packet で URB が完了するので、
その都度 URB 再投入の往復が入る。これも `stalls` と帯域の逆相関を作る。

**切り分け方の提案.** device 側の `stalls` だけでなく、**host 側で 1 URB あたりの実転送長の
分布**を取ってほしい。512 の倍数で終わる URB に混じって**短く終わる URB が多い**なら ZLP 起因、
URB は満額で返っているのに遅いならタスク往復起因である。前者なら CR-7 の
「転送を長くする」(下記)がそのまま効く。

---

## CR-6 arduino-cli と symlink

`docs/troubleshooting.md` / `.ja.md` の「4. ビルドと書き込み」に項目を足した。
併せて `build_opt.h` が効かないように見えるとき(置き場所と `--clean`)も同じ章に書いた。

---

## CR-7 転送を 2 つ以上 in-flight に

**「そもそも可能か」への回答: TinyUSB を触らずに、ほぼ同じ効果が今日試せる。**

TinyUSB の vendor class が endpoint ごとに 1 転送しか投げないのは指摘のとおりである。
ただし**1 転送 = 1 packet ではない。** 実際に投げる長さを決めているのは

```c
// class/vendor/vendor_device.h
#ifndef CFG_TUD_VENDOR_TX_EPSIZE
  #define CFG_TUD_VENDOR_TX_EPSIZE TUD_EPSIZE_BULK_MAX   // HS では 512
#endif
```

で、`vendord_open()` はこれを `tu_edpt_stream_open(tx_stream, ..., CFG_TUD_VENDOR_TX_EPSIZE)` の
`xfer_len` として渡す。`tu_edpt_stream_write_xfer()` は FIFO からこの長さまで引き抜いて
`usbd_edpt_xfer()` に渡す。**つまり既定では「1 転送 = 512 byte = 1 packet」で、
毎パケットごとに完了割り込みと usbd タスク往復が入っている。**
1 microframe あたり 2.4 transaction という実測は、1 往復あたり約 52 us という意味になり、
これはタスク往復の時間として妥当な桁である。

DWC2 は 1 転送で複数 packet を扱える(`DIEPTSIZ` の pktcnt は 10 bit)。
`CFG_TUD_VENDOR_TX_EPSIZE` を 4096 にすれば、**1 回の arm で 512 byte × 8 packet が
CPU 介入なしに連続して出る**。往復回数は 1/8 になる。
そして **この値は元から `#ifndef` で囲われている**ので、ライブラリの変更は要らない。

```
# build_opt.h
-DCFG_TUD_VENDOR_TX_BUFSIZE=8192
-DCFG_TUD_VENDOR_TX_EPSIZE=4096
```

代償は `TUD_EPBUF_DEF(epin, CFG_TUD_VENDOR_TX_EPSIZE)` の静的 buffer がその分太ること
(4096 なら +3,584 byte)。

**お願い: E071 の台でこの 1 行を振ってほしい。** FIFO 深さ(8 KiB)を入れたうえで
`TX_EPSIZE` を 512 / 1024 / 2048 / 4096 / 8192 と変える。FIFO を深くしても
`write()` の spin が 1 packet あたり 3.5 回で下げ止まった、という観測に対して、
**これが効けば原因は「転送が短い」で確定**する。効かなければ本当に in-flight 2 本が要る、
という切り分けになる。

**併せて既定値を動かさなかった理由。** こちらには HS host が無く、
`TX_EPSIZE` を上げた効果を測れない。測っていない値を既定にはできないので、
既定は TinyUSB のまま(512)にしてある。**測定値が出たら既定を上げる。**

依頼書にある host 側 async URB(depth 2〜8)の測定も、上の URB 実転送長の分布と
同時に取れるはずなので、そちらを先にやる方針で問題ないと思う。

---

## CR-8 HID の packet size

3 点とも入れた。

1. `CFG_TUD_HID_EP_BUFSIZE` に `#ifndef` ガードを付けた。
2. `EspUsbDeviceHidVendor::begin()` と `configurationDescriptor()` の 64 固定をやめた。
   上限は `CFG_TUD_HID_EP_BUFSIZE - 1`(`tud_hid_n_report()` が先頭 1 byte を report ID に
   使うため)。`EspUsbDeviceHidVendor::maxReportSize()` で取れる。
3. **P4 の既定を 512 にした。** ちなみに **core 内蔵 stack は P4 で元から 512 だった**
   (`CONFIG_TINYUSB_HID_BUFSIZE=512`)。64 に縛っていたのはこのライブラリだけである。

**依頼書に無かったが必要だった修正がある。** `VENDOR_REPORT_DESCRIPTOR` は
**Report Count (63) が焼き込まれた定数**だった。endpoint だけ 512 にしても、
report descriptor が 63 byte と宣言したままでは**ホストは 64 byte しか読まない**。
E073 は host が `EspUsbHost` の raw transfer だったので通ったが、
**依頼書にある「Linux の hidraw で確かめられる」はこのままでは成立しない。**
report descriptor を instance ごとに組み立て、Report Count が `reportSize` に追従するようにした
(255 超は 2 byte 形式 `0x96 lo hi`)。

descriptor は速度ごとに分かれる。FS 側は interrupt endpoint の上限 64、HS 側は
`reportSize + 1`(上限 `CFG_TUD_HID_EP_BUFSIZE`)。**HID function 単位で書き換えている**ので、
vendor HID の隣にあるキーボードは 8 byte のままである。interrupt endpoint は帯域を予約するので、
8 byte しか送らないものを膨らませるとバス上の他から予約を奪うことになる。

```cpp
EspUsbDeviceHidVendor vendor(device, 511);   // P4
```

`bInterval` は従来どおり 1 のままにした。HS では 125 us 周期という意味になり、
E073 の 8,046 report/s はこれによる値なので、4(= 1 ms)に「直す」と 8 分の 1 になる。

**確認の取り方.** E073 をライブラリのコピーなしで再実行。`wMaxPacketSize` が HS の
configuration descriptor で 512、FS 側で 64 になっていること。今回 P4 実機で
descriptor が期待どおり組まれることまでは確認した(`tests/single/p4_hs_packet_sizes`)が、
**4.1 MB/s が出るかは HS host が要るのでそちらでお願いしたい。**

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
`bytes` は `writeCapacity()` で clamp する(FIFO より大きい要求は永遠に満たされないため)。
`timeoutMs == 0` は非ブロッキングの 1 回確認。mount されていなければ false を返す
(誰も FIFO を掃き出さないので)。

```cpp
while (sent < total) {
  if (!HsVendor.waitWritable(512, 100)) { break; }   // 抜き差しで抜ける
  sent += HsVendor.write(data + sent, total - sent);
}
```

**E078 で効くはずだが、こちらでは測れていない。** P4 の loopback は host 役と device 役が
同じ CPU に載っているので、`waitWritable()` で送出タスクを止めると host 側も止まる——
実機で意味のある測定にならない。**PC を host にした台でお願いしたい。**

---

## 作業中に見つかった別件

**複合 HID に `EspUsbDeviceHidVendor` を混ぜると report descriptor が壊れる。**
`EspUsbDevice::buildDescriptors()` の複合 HID merge は、各クラスの report descriptor の
**先頭 6 byte を prologue と決め打ち**して、その後ろに Report ID item を差し込む。
keyboard / mouse は `05 xx 09 xx a1 01` でちょうど 6 byte だが、vendor HID の usage page は
3 byte item(`06 00 ff`)なので prologue が 7 byte あり、しかも Report ID を自前で持っている。
結果、6 byte で切った続きに Report ID を挿すと item 境界がずれる。

**今回は直していない。** CR の範囲外であり、`EspUsbDeviceHidVendor` は examples / tests とも
単独でしか使われていないので現に踏まれていない。直すなら merge 側を item 単位で走査する形に
変える必要があり、複合 HID 全体の回帰確認が要る。**別途起票したい。**

---

## このリポジトリで確認したこと

| 対象 | 実機 | 内容 |
|---|---|---|
| `tests/unit/tinyusb_config` | 不要 | buffer サイズの `#ifndef` が効くこと、32768 超がビルドエラーになること |
| `tests/single/descriptor` | ESP32-S3 | MS OS 2.0 の flat / subsets、vendor の per-speed MPS、HID vendor の Report Count |
| `tests/single/p4_hs_packet_sizes` | ESP32-P4 | 511 byte report の descriptor、HS 512 / FS 64、8 KiB FIFO、observer の着脱 |
| `tests/loopback/usb_vendor` | ESP32-P4 | host 役が実際に取りに行った MS OS 2.0 が 162 byte であること、observer が MS OS 2.0 要求と GET_DESCRIPTOR を見ていること |
| `tests/loopback/` 全体 | ESP32-P4 | 既存 17 本の回帰 |
| `tools/build_check.py` | 不要 | esp32s3 / esp32s2 / esp32p4 の全 example |

**HS での帯域(CR-4 / CR-7 / CR-8)と Windows での driver bind(CR-1)は、
こちらの台では測れない。** P4 の loopback は host 役が FS ポートなので、リンクは常に
full speed になる。
