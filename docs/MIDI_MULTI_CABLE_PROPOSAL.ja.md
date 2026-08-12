# EspUsbDevice MIDI 複数 cable 対応 仕様案

> **状態: 実装済み（cable 数と方向別 cable 数。cable 名は未実装）。** 統合ライブラリ
> `/home/mt/dev/EspMidi/` からの要求。既存 API を変えない純粋な追加として設計している。
>
> 提案内容をコードと照合した結果と、実装時に提案から変えた点を末尾の
> 「レビュー結果と実装」に記録している。提案本文はレビュー時のまま残しているので、
> 数値の誤り（1 cable = 88 バイト）もそこで訂正している。

## 目的

`EspUsbDeviceMidi` が、1 つの MIDI インターフェース上に **複数の cable(仮想 MIDI ポート)** を公開できるようにする。あわせて cable ごとに名前を付けられるようにする。

USB MIDI 1.0 では 1 インターフェースが最大 16 cable を持てる。PC 側からは cable が「MIDI ポート」として個別に見え、DAW のポート一覧に別々の項目として並ぶ。

## 現状

`EspUsbDeviceMidi::configurationDescriptor()`(`src/EspUsbDevice.cpp:2566`)は TinyUSB の 1 本用テンプレートをそのまま使っている。

```cpp
const uint8_t descriptor[] = {
    TUD_MIDI_DESCRIPTOR(interfaceNumber, 0, epOut, epIn, endpointSize),
};
```

このマクロは `src/device/usbd.h:400` で次のように定義されており、**cable 1 本・jack 名なし固定**である。

```c
#define TUD_MIDI_DESCRIPTOR(_itfnum, _stridx, _epout, _epin, _epsize) \
  TUD_MIDI_DESC_HEAD(_itfnum, _stridx, 1),\
  TUD_MIDI_DESC_JACK_DESC(1, 0),\
  TUD_MIDI_DESC_EP(_epout, _epsize, 1),\
  TUD_MIDI_JACKID_IN_EMB(1),\
  TUD_MIDI_DESC_EP(_epin, _epsize, 1),\
  TUD_MIDI_JACKID_OUT_EMB(1)
```

一方で、**パケット API 側は既に cable に対応している**。`readPacket()` / `writePacket()` が扱う 4 バイトパケットの `header` は上位ニブルが cable 番号、下位ニブルが CIN である。TinyUSB の MIDI ドライバはバルクエンドポイント上のバイト列を運ぶだけで cable を解釈しない。

つまり **ホストに何ポート見せるかを決めているのは descriptor だけ**で、他は既に揃っている。

## 提案

### 1. cable 数をコンストラクタで受ける

```cpp
class EspUsbDeviceMidi : public EspUsbDeviceClass
{
public:
  // cableCount は 1..16。既定 1 で現在の挙動と完全に一致する。
  explicit EspUsbDeviceMidi(EspUsbDevice &device, uint8_t cableCount = 1);
```

既定値 1 なので、既存のスケッチ・example・テストは無変更で動く。

### 2. descriptor を cable 数だけ組み立てる

必要なマクロは TinyUSB 側に全て揃っており、いずれも cable 数で parameterize されている(`src/device/usbd.h:351-395`)。

```c
TUD_MIDI_DESC_HEAD(itf, stridx, N)          // N = cable 数
TUD_MIDI_DESC_JACK_DESC(cablenum, stridx)   // cable ごとに 1 回
TUD_MIDI_DESC_EP(ep, epsize, N)             // エンドポイントごとに 1 回
TUD_MIDI_JACKID_IN_EMB(cablenum)            // OUT エンドポイントの後に N 個
TUD_MIDI_JACKID_OUT_EMB(cablenum)           // IN エンドポイントの後に N 個
```

生成する並びは次のとおり。

```text
TUD_MIDI_DESC_HEAD(itf, stridx, N)
TUD_MIDI_DESC_JACK_DESC(1, str1) ... TUD_MIDI_DESC_JACK_DESC(N, strN)
TUD_MIDI_DESC_EP(epOut, epSize, N)  +  TUD_MIDI_JACKID_IN_EMB(1..N)
TUD_MIDI_DESC_EP(epIn,  epSize, N)  +  TUD_MIDI_JACKID_OUT_EMB(1..N)
```

固定長マクロの列挙ではなくバイト列を順に書き出す形になるが、`configurationDescriptor()` は既に `uint8_t *dst` へ書き込む関数なので、シグネチャは変わらない。

`interfaceCount()`(2)と `endpointCount()`(1)は cable 数に関係なく変わらない。cable は 1 組のバルクエンドポイントを多重化する概念なので、エンドポイントは増えない。

### 3. cable ごとの名前(任意)

`TUD_MIDI_DESC_JACK_DESC(_cablenum, _stridx)` の第 2 引数が jack の string index である。ここに文字列を割り当てると、PC 側のポート一覧に個別の名前が出る。

```cpp
// 例: setCableName(1, "Synth") / setCableName(2, "Drums")
bool setCableName(uint8_t cableNumber, const char *name);
```

string descriptor テーブルへの登録が必要なので、実装は `EspUsbDevice` 側の文字列管理に依存する。**この項目は cable 数対応より優先度が低い**ので、cable 数だけ先に入っても EspMidi 側は困らない。

## 注意点

**descriptor サイズ。** 16 cable のときの MIDI descriptor 長は次のようになる。

```text
TUD_MIDI_DESC_HEAD_LEN                =  34
TUD_MIDI_DESC_JACK_LEN * 16           = 480  (30 * 16)
TUD_MIDI_DESC_EP_LEN(16) * 2          =  58  (29 * 2)
                                 合計 = 572 バイト
```

現在の 1 cable では 88 バイト(`TUD_MIDI_DESC_LEN`)なので、**設定ディスクリプタを組み立てるバッファが 572 バイト + 他クラス分を収容できるか確認が必要**。足りない場合はバッファ拡張もこの変更に含まれる。

## 要求元の状況

`EspMidi` は複数の MIDI インターフェースを統合するライブラリで、内部モデルを次の 2 階層にしている。

```text
Endpoint(接続の単位)
 └─ Port(最大 16。USB では cable、MIDI 2.0 では group に対応)
```

1 エンドポイントあたり最大 16 ポートという上限は、USB MIDI 1.0 の cable(4 bit)と MIDI 2.0 の group(4 bit)が一致するために選んでいる。

現状の 1 cable 固定だと、USB Device 側だけがこのモデルに乗れず「ESP32 を複数ポートの MIDI インターフェースとして PC に見せる」構成が作れない。これは EspMidi の主要ユースケース(複数の外部 MIDI 機器を 1 台の ESP32 経由で PC の別ポートとして見せる)そのものにあたる。

## テスト

既存の `tests/loopback/usb_midi` と `tests/peer/usb_midi` に、cable 番号を跨いだ往復を追加すれば足りる。パケットの `header` 上位ニブルを変えて送り、受信側で同じ cable 番号が復元されることを確認する。

`tests/loopback/usb_midi/test_loopback_usb_midi.py` は既に `MIDI_RX cable=0 cin=09 ...` の形で cable を出力しているので、期待値に cable=1 以上のケースを足す形になる。

## MIDI 2.0 との関係

`src/device/usbd.h:409-429` に USB-MIDI 2.0(UMP)用の descriptor テンプレート(`TUD_MIDI2_DESC_ALT1_HEAD` / `TUD_MIDI2_DESC_ALT1_EP`、Group Terminal Block 対応)が既に入っている。

今回の cable 数対応は Alt Setting 0(MIDI 1.0)側の話で、UMP 側とは独立している。ただし cable 数と UMP の group 数は同じ上限(16)なので、将来 Alt Setting 1 を有効にする際も「ポート数」の指定はそのまま流用できる形になる。

## レビュー結果と実装

提案の技術的前提は、次のとおりコードと一致していた。

- descriptor だけが cable 数を決めている。`midid_open()`(`src/class/midi/midi_device.c:450`)
  は CS descriptor を長さで読み飛ばし、`bNumEndpoints` 分だけ endpoint を開くだけで、
  jack を解釈しない。
- パケット API は既に cable 対応。`readPacket()` / `writePacket()` は生バイトを
  `tud_midi_packet_read/write` へ渡すだけである。
- 必要なマクロは全て `_numcables` で parameterize 済み。MS Header の `wTotalLength` も
  `src/device/usbd.h:357` で cable 数から計算されている。
- `interfaceCount()`=2 / `endpointCount()`=1 は変わらない。

提案から変えた点は次の 4 つ。

**1. 「現在 1 cable で 88 バイト」は誤りで、実際は 92 バイト。**
`TUD_MIDI_DESC_EP_LEN(1)` = 9+4+1 = 14 なので 34+30+14×2 = 92 である。
`src/device/usbd.h:389` の `// Length of template descriptor (88 bytes)` という
コメント自体が stale だった(EP_LEN はマクロ外で出力される jack ID の 1 バイトを含む)。
16 cable = 572 バイトの方は提案どおり正しい。

**2. バッファは「確認が必要」ではなく明確に不足していた。**
`MAX_CONFIG_DESCRIPTOR` は 256 バイトで、しかもこのサイズの配列を device あたり
3 本持っている。704 バイトへ拡張した。

ただしこれを `EspUsbDevice` のメンバ配列のまま拡張すると、オブジェクトが 1344 バイト
大きくなる。`tests/unit/descriptor` の class lifecycle ケースは `EspUsbDevice` を 4 個
同時にスタックへ置くため、実機で毎回 `Stack canary watchpoint triggered (loopTask)` の
panic になった。そこで **3 本の buffer をヒープへ移し**、最初の `buildDescriptors()` で
1 回だけ確保してデストラクタで解放する形にした。結果としてオブジェクトは cable 対応前
より小さい。スケッチは device をグローバルに置く想定だが、スタック上のインスタンスは
従来動いており、テストを変えて済ませる話ではない。あわせて `EspUsbDevice` をコピー不可に
した(グローバルな callback target に自身を登録する型なのでコピーは元々意味がなく、
この宣言で二重解放がビルドエラーになる)。

**3. これは「足りない」ではなく「サイレントにオーバーランする」問題だった。**
提案に書かれていなかった一番重要な点である。呼び出し側は
`configurationDescriptorForSpeed()` に容量を渡しているが、基底のデフォルト実装が
その引数を捨てて `configurationDescriptor()` へ転送していた。MIDI 側で
`configurationDescriptorForSpeed()` を override し、書き込む前に容量を検査して
収まらなければ 0 を返すようにした。descriptor は `dst` へ直接書き、572 バイトの
スタック配列も作らない。呼び出し側が事前にサイズを知れるよう
`descriptorLength()` を公開した。

**4. 便利メソッドが cable 0 固定だった。**
`noteOn()` などが `packet.header = CIN` で cable ニブルを常に 0 にしていたため、
cable を公開しても helper は cable 1 にしか出せなかった。全 helper に 0 始まりの
`cable` 引数(既定 0)を追加し、`cableCount()` 以上の cable は「Host が知らない port に
載る packet を出す」代わりに false を返すようにした。

### cable 名(§3)を実装しなかった理由

`stringDescriptor()`(`src/EspUsbDevice.cpp:1004`)は index 1/2/3 のハードコードな
if 連鎖に、Net が有効なときだけ index 4 が MAC 文字列として加わるだけの実装で、
文字列テーブルも index 割り当ての仕組みも無い。**index 4 は Net と衝突する。**
提案の「EspUsbDevice 側の文字列管理に依存する」ではなく、文字列テーブルと index
アロケータの新規実装が前提になる。提案どおり cable 数を先に入れ、名前は Host 側の
命名に任せた。なお `TUD_MIDI_DESC_JACK_DESC` は 1 つの stridx を 4 つの jack すべてに
付けるので、jack 単位の名前指定はそもそもできない。

### テスト

提案は「既存の loopback / peer に cable 跨ぎを足せば足りる」としていたが、往復テスト
だけでは足りない。**受信 message の cable 番号は packet header をそのまま読んだ値**なので、
1 cable の descriptor でも cable=3 として往復してしまう。そのため:

- `tests/unit/midi_descriptor`: 実際の descriptor 生成コードを g++ でコンパイルし、
  cable 16 通りすべてについて全フィールドを検証する。1 cable のときは
  `TUD_MIDI_DESCRIPTOR()` と byte 単位で一致することも確認する。
- `tests/loopback/usb_midi_cables`: 4 cable の device を P4 1 台で双方向に検証する。
- `tests/peer/usb_midi_cables`: **非対称**(device → Host 4 本、Host → device 5 本)の
  device を 2 台構成で検証する。device が実際に cable を申告したことを実機で示せるのは、
  EspUsbHost が descriptor から読み取った cable 数(`getMidiPortInfo()`)だけなので、これを
  **MIDI 通信の前**と全 cable に通信を流した後の両方で確認する(観測した packet から数を
  積み上げる実装なら失敗する)。さらに 1 回の bulk transfer に異なる cable の packet を複数
  入れる場合、0 以外の cable での SysEx、受信専用 cable への送信が失敗することもカバーする。
  非対称にした理由は後述。`getMidiPortInfo()` が未リリースのため profile は `s3_peer_local` のみ。
  16 ではなく 5 が上限なのは後述の Host 側制約による。
- 既存の `usb_midi` は既定の 1 cable を引き続きカバーする。

peer test でカバーできない項目が 2 つ残る。いずれも `EspUsbDeviceMidi` がその device を
作れないためで、将来の composite peer sketch に委ねている。

- **MIDI 以外の endpoint に付く CS_ENDPOINT。** Audio の isochronous endpoint にも付くので、
  方向 latch の漏れが出るのは Audio + MIDI の composite である。
- **MS インターフェースを 2 つ持つ device。** `EspUsbDeviceMidi` は 1 device に 1 つしか
  持てない。

### 追加実装: 方向別の cable 数

当初は提案どおり cable 数を 1 つだけ受けていたが、それでは **Host 側の方向マッピングを
検証できない**ことが分かったため、`EspUsbDeviceMidi(device, inCableCount, outCableCount)`
を追加した。

クラス仕様は embedded jack を **device 側から見て**命名するので、bulk **IN** endpoint に付く
CS_ENDPOINT が列挙するのは Embedded MIDI **OUT** Jack である。したがって Host が 2 方向を
入れ替えていても、対称な device では気付けない。往復テストでも気付けない——受信 message の
cable 番号は、その packet 自身の header から読んだ値だからである。**非対称な device だけが
この区別を可視化できる。**

名前は Host から見た方向に揃えた(USB の endpoint 方向、および EspUsbHost の
`EspUsbHostMidiPortInfo` と同じ)。IN が device → Host、OUT が Host → device である。
送信 helper の上限は `inCableCount()` なので、受信専用の cable への送信は失敗する。

descriptor 側の実装:

- 両方向に存在する cable は `TUD_MIDI_DESC_JACK_DESC` の 4 jack 一組(30 バイト)をそのまま
  使う。よって対称な構成のバイト列は変わらない。
- 片方向だけの cable は、TinyUSB の jack マクロが必ず 4 jack を出力してしまうため使えない。
  本ライブラリ側の `MIDI_DESC_JACK_IN_ONLY` / `MIDI_DESC_JACK_OUT_ONLY` で、その方向に必要な
  2 jack(external + embedded、15 バイト)だけを出力する。embedded jack には必ず source が
  必要なので、対になる external jack は残す。
- jack ID は従来と同じ per-cable の式から採るので、欠けた方向の ID は単に使われない。
  jack ID に求められるのは一意性であって連続性ではない。
- MS header の `wTotalLength` は、`TUD_MIDI_DESC_HEAD` が単一の cable 数から算出するため
  非対称を表現できない。実際に書いた長さで上書きする。対称時は同じ値になり、それは
  1 cable が `TUD_MIDI_DESCRIPTOR()` と byte 単位で一致するテストが保証している。

実機結果: 4-in / 5-out の device に対し EspUsbHost は `MIDI_PORT_INFO ok=1 in=4 out=5 iface=1`
を返した。方向マッピングは正しい。

### 実機で判明した Host 側の制約: EspUsbHost では 5 cable が上限

最初は peer test を 16 cable で組んだが、device が enumerate せず全滅した。

```text
E (9791) ENUM: Configuration descriptor larger than control transfer max length
E (9791) ENUM: [0:0] CHECK_SHORT_CONFIG_DESC FAILED
```

ESP-IDF の USB Host は、enumeration の control transfer より長い configuration descriptor を
拒否する。`CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE` は Arduino のプリコンパイル済み
ライブラリで **256 固定**（USB Host 対応の全ターゲットの sdkconfig で確認）で、スケッチから
変更する手段が無い。

実機で測った境界は次のとおり。

| cable 数 | MIDI descriptor | +config header | EspUsbHost |
| --- | --- | --- | --- |
| 4-in / 5-out | 204 | 213 | enumerate する |
| 5 | 220 | 229 | enumerate する |
| 6 | 252 | 261 | `CHECK_SHORT_CONFIG_DESC FAILED` |
| 16 | 572 | 581 | `CHECK_SHORT_CONFIG_DESC FAILED` |

6 cable のとき device 側は `DEVICE_READY cables=6 bytes=252` を出しており、descriptor の生成は
成功している。拒否しているのは Host である。また device がまったく現れないので、部分的に
enumerate されるようなケースでもない。

したがって:

- **本ライブラリ側の制約ではない。** 16 cable は USB 仕様上正当で、PC の Host は受け付ける。
  descriptor の byte 検証は `tests/unit/midi_descriptor` が 16 通りすべてカバーしている。
- **EspUsbHost と組み合わせる構成では 5 cable が上限。** EspMidi が USB Device 側と USB Host
  側を同一構成で繋ぐ場合、ここが効く。README に記載した。
- MIDI に限らず、descriptor が長い function すべてに当てはまる（16 cable + 他 class の
  composite なら、なおさら早く当たる）。

実機テスト 2 件は実行済み（`peer/usb_midi_cables` 10 件、`loopback/usb_midi_cables` 1 件、いずれもパス）。既存の `peer/usb_midi`(6 件) と `loopback/usb_midi`(1 件) も回帰確認済み。

### 提案の残りの前提について

「EspUsbHost が複数 cable の descriptor を enumerate できるか」は懸念として挙げていたが、
EspUsbHost 側は `espUsbHostMidiEndpointCableCount()` で MS endpoint descriptor から
cable 数を復号し、`message.cable = packet[0] >> 4` で cable 番号も復号している。
往復に必要な部分はリリース済み(`message.cable`)で、cable 数の discovery だけが未リリース。

MIDI 2.0(§MIDI 2.0 との関係)については提案どおり独立で、今回は Alt Setting 0 のみを
触っている。
