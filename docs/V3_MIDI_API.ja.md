# EspUsbDeviceMidi API（v3）

USB-MIDI 1.0 の Function です。MIDI の1イベント（4 byte の USB-MIDI packet）単位で送受信します。
内部では共通の bulk 転送層を使い、TinyUSB の MIDI class と固定 FIFO は使いません。
容量・cable 数・受信の通知方法は Function ごとに設定でき、再ビルドは要りません。

## 基本の使い方

```cpp
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceMidi midi(device);  // cable 1本（送受信とも）

void setup() {
  EspUsbMidiConfig config = midi.configuration();  // constructor の cable 数を引き継ぐ
  config.name = "MIDI Interface";
  if (midi.configure(config) != ESP_OK) return;
  device.begin();
}

void loop() {
  EspUsbDeviceMidiPacket packet;
  while (midi.readPacket(packet)) {
    // 受け取った1イベントを処理する。別 task へ渡すなら 4 byte をコピーする。
  }
  midi.noteOn(0, 60, 100);  // channel 0（0〜15）, note 60, velocity 100, cable 0
}
```

### 主な設定

`EspUsbMidiConfig` の項目です。方向は USB の慣例どおり Host から見た名前です（IN＝Device→Host＝このデバイスが送る、OUT＝Host→Device＝このデバイスが受ける）。
MIDI class の規格書は embedded jack を Device 側から見て名付けているので、逆向きになる点に注意してください。

| 項目 | 既定値 | 意味 | FS/HSでの違い |
| --- | --- | --- | --- |
| `inCables` / `outCables` | 1 / 1 | 送信側・受信側の仮想 cable 数。各 1〜16 | なし |
| `name` | `nullptr` | Function 名（UTF-8）。begin 時に文字列 descriptor へコピー | なし |
| `rxPackets` | 128 | 受信 ring の容量（イベント数。1イベント＝4 byte） | なし |
| `txPackets` | 128 | 送信 ring の容量（イベント数） | なし |
| `transferPackets` | 0（自動） | 1回の送信転送に載せる最大イベント数。0 は bus packet 1個分 | 自動の値は FS 16イベント（64 byte）、HS 128イベント（512 byte） |
| `rxMode` | `Queue` | 受信方法。`Queue`、`Callback`、`Disabled` | なし |
| `autoFlush` | `true` | false にすると `flush()` を呼ぶまで送信を始めない | なし |
| `streamTermination` | `FunctionDefault` | 送信の区切り方。MIDI の既定は短い packet で区切り、ring の末尾が MPS の倍数なら長さ0の packet（ZLP）も送る | なし |
| `rxBuffer` / `txBuffer` | `nullptr` | 利用者が用意する ring 用配列。nullptr なら begin で確保 | なし |

MPS は bulk endpoint の最大 packet 長です（FS 64 byte、HS 512 byte）。
`transferPackets` を明示した場合は MPS より小さくても受け付けます。

## 設定の変え方

- `configure()` と `onPacket()` は停止中だけ成功します。動作中は `ESP_ERR_INVALID_STATE` を返し、それまでの設定を保ちます。
- 設定を変えるときは `device.end()` → `configure()` → `device.begin()` の順に呼びます。
- `configure()` はすべての項目を置き換えます。constructor で指定した cable 数を残したいときは、`midi.configuration()` の値をコピーしてから変更します。
- constructor は cable 数を1つ（送受信共通）指定する形と、送信・受信を別々に指定する形があります。
- `configuration()` は要求した値をそのまま返します。`transferPackets = 0` のような自動指定も 0 のまま返ります。

`configure()` の戻り値：

| 戻り値 | 条件 |
| --- | --- |
| `ESP_OK` | 受け付けた |
| `ESP_ERR_INVALID_STATE` | 動作中 |
| `ESP_ERR_INVALID_ARG` | cable 数が 0 または 17 以上、`rxMode` が範囲外、`Callback`/`Disabled` なのに `rxPackets` か `rxBuffer` を指定、容量が大きすぎる、`rxBuffer` と `txBuffer` の領域が重なる など |
| `ESP_ERR_INVALID_SIZE` | `transferPackets` が 16383 を超える（1回の転送長が 16 bit に収まらない） |

cable 数は丸めずに拒否します。上限の 16 は、USB-MIDI 1.0 の packet header で cable 番号が 4 bit だからです。

`device.begin()` の後で MIDI が動いていないときは `midi.startError()` で理由を確認できます。`rxMode = Callback` なのに `onPacket()` を登録していない場合は `ESP_ERR_INVALID_ARG`、メモリ不足は `ESP_ERR_NO_MEM` です。

## 送信

| 関数 | 戻り値 |
| --- | --- |
| `writePacket(packet)` | イベントを送信 ring へコピーできたら true |
| `noteOn` / `noteOff` / `controlChange` / `programChange` / `polyPressure` / `channelPressure` / `pitchBend` | 同上。最後の引数 `cable` は 0 始まりで既定 0 |
| `availableForWritePackets()` | 今すぐ書けるイベント数 |
| `sendCapacity()` | 確保した送信 ring のイベント数（停止中は 0） |
| `transferCapacity()` | 1回の送信転送の実際の容量（イベント数、停止中は 0） |

- 送信は 4 byte 単位で受け付けます。ring に空きがなければ false を返し、イベントの一部だけを受け付けることはありません。
- 複数の task から同時に送っても、イベント同士の byte が混ざることはありません。
- true は「ring へコピーした」という意味で、Host が受け取ったことは意味しません。戻った後は元の packet を再利用できます。
- 送信先 cable が `inCables` 以上のとき、または CIN（Code Index Number、header の下位 4 bit）が予約値の 0 か 1 のときは false を返し、ring へ入れません。Host に知らせていない cable へ黙って送ることはありません。
- 便利関数（`noteOn` など）は channel を 15 まで、note や値を 7 bit の範囲へ丸めます。
- `txPackets = 0` にすると送信 ring と送信用 DMA 領域を確保せず、送信は常に false になります。

## 受信

| 関数 | 戻り値 |
| --- | --- |
| `readPacket(packet)` | 1イベント読めたら true |
| `availablePackets()` | 今読めるイベント数 |
| `receiveCapacity()` | 確保した受信 ring のイベント数（停止中は 0） |
| `receiveErrors()` | 捨てた不正イベントと端数の累計。begin で 0 に戻る |
| `onPacket(callback)` | `rxMode = Callback` 用の通知先を登録。停止中だけ `ESP_OK` |

- 受信した USB packet のうち、cable が `outCables` 以上のもの、CIN が 0 か 1 のもの、4 byte に満たない端数は捨て、`receiveErrors()` を1ずつ増やします。同じ packet 内の正しいイベントは順番どおりに渡します。
- これは MIDI メッセージや SysEx の中身を検査するものではありません。正しいイベントの中身は変更しません。
- 受信 ring より大きな USB packet が届いた場合、残りは DMA 用の packet 領域に保持し、`readPacket()` で ring が空くたびに移します。全部読み終えるまで次の packet は受けません。このため ring 容量が1でもイベントを失わず、`availablePackets()` が ring 容量を超えることがあります。

### callback で受ける

`rxMode = Callback`、`rxPackets = 0` にして、停止中に `onPacket()` を登録します。

- callback には1イベントの const 参照が渡り、その呼び出しの間だけ有効です。後で使うなら 4 byte をコピーします。
- callback は USB worker task で動きます。block する処理、`end()`、設定変更は呼ばないでください。
- callback の中の `writePacket()` も待たずに戻り、空きがなければ false です。確実に送りたい場合は送信容量を十分に取るか、アプリ側で保留してください。

### 片方向だけ使う

- 送信専用：`rxMode = Disabled`、`rxPackets = 0`。受信 ring と受信用 DMA 領域を確保しません。
- 受信専用：`txPackets = 0`。送信側を確保しません。
- descriptor には bulk endpoint の IN/OUT 両方が載るので、endpoint 数の予算は両方向分が必要です。

## FSとHSの違い

| 項目 | FS（S3 など） | HS（P4/P4X の HS controller） |
| --- | --- | --- |
| bulk の MPS | 64 byte | 512 byte |
| `transferPackets = 0` の実際の値 | 16イベント | 128イベント |
| `transferPackets` に MPS 未満の値 | 受け付ける | 受け付ける |

自動の値は、選んだ controller が HS に対応するかで決まります。MIDI に関わるそのほかの制約は FS と HS で同じです。

## PSRAMとバッファ

`rxBuffer` / `txBuffer` は DMA を使わない ring なので、利用者が用意した配列を渡せます。指定した容量（イベント数×4 byte）の領域を begin から end まで有効に保ってください。
ライブラリは渡された領域を解放せず、黙って拡張もしません。USB DMA の受け渡し用領域は、ライブラリが内部 RAM に確保します。
PSRAM 上の ring は Vendor で確認済みです（[Vendor API](V3_VENDOR_API.ja.md)）。MIDI の ring を PSRAM に置く構成は実機で試していません。

## 他の制約

- MIDI は `Print` でも `Stream` でもありません。byte 単位の `write()` や `read()` は公開しておらず、誤った型変換はコンパイル時に拒否されます。送信の空きは `availableForWritePackets()` でイベント単位に確かめます。
- MIDI 2.0（UMP）は対象外です。
- 文字列の寿命と UTF-8 の制限は[文字列ガイド](V3_TEXT_DESCRIPTORS.ja.md)に従います。
- MIDI を使わないプログラムには、この実装も TinyUSB の MIDI FIFO もリンクされません。ただし bulk 系 Function で共有する管理情報は残ります。

## 2.x からの移行

- `CFG_TUD_MIDI_*`（FIFO や endpoint buffer の指定）と `CFG_TUD_MIDI`（0 を含む）は廃止しました。容量は `EspUsbMidiConfig` で指定します。
- TinyUSB の `tud_midi_*` を直接呼んでいたコードは、`readPacket` / `writePacket` などへ移してください。
- cable 数に 0 や 17 以上を指定していたコードは、拒否されるようになったので直す必要があります。
- `readPacket` / `writePacket` / `noteOn` などの呼び方は 2.x と同じです。単一の global instance はなくなりました。

## 設計判断の理由

- 利点：ビルド設定なしで再構成できる、MIDI Function を複数持てる、方向ごとに資源を省ける、小さな queue で動く、型付きの callback が使える。
- 不利益：begin 時に動的確保が起きる。MPS より小さい ring では小分けにコピーする。不正な cable/CIN を拒否するので移行の手間がある。
- byte stream ではなくイベント単位の API にしたのは、1イベントが分割されないことを型で保証するためです。以前は Print へのアップキャストを実行時に拒否していましたが、コンパイル時に拒否する形へ変えました。共通 bulk 層を実装に使うことは、公開 API を byte stream にするという意味ではありません。

## 実測の記録

2026-10-26、S3 同士（FS）で `validation/s3/midi_stream` を実行しました。Host 側に専用の patch は入れていません。

- 確認した内容：容量1、37/43 イベントの境界、外部 ring、callback、片方向、MIDI 2つ、MIDI＋CDC＋Keyboard の複合。2つ目の MIDI は Host の標準 MIDI API が最初の interface しか扱わないため、interface を明示する bulk API で確認した。
- 全11件が成功（`runtime-midi-all.xml`、586.92 秒）。送信の検査を bulk lock の中へそろえた後、関連3件も成功（`midi-stream-final.xml`、192.03 秒）。
- 2 task から計200イベントを送り、順序と欠けがないことを確認。313 箇所で確保失敗を起こし、失敗と回復を確認。
- IN 16 / OUT 1 の非対称構成で cable 15 への送信、全便利関数、値の丸め、SysEx/リアルタイムの15イベントを byte 単位で照合。
- 送信 task を動かしたまま end し、停止中に cable 数を32回変えても、終了後の送信が受け付けられないことを確認。
- 全件で両ボードが STOPPED、panic なし。最小構成4種の nm で、未使用 Function と TinyUSB の CDC/Vendor/HID/MIDI が含まれないことも確認。

2026-10-31、`midi_stream` を含む S3 の peer 試験を P4/P4X の HS 直結でも収集しました（結果は [P4/P4X と追加試験環境](V3_P4_VARIANTS_AND_BENCH.ja.md#s3-peer試験をhs直結へ広げた結果2026-10-31)）。
各 OS の音楽アプリとの相互接続は未確認です。

規格：[USB-MIDI 1.0](https://www.usb.org/sites/default/files/midi10.pdf)
