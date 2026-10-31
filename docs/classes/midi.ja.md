# USB MIDI 1.0

[English](midi.md)

## 概要と使う場面

`EspUsbDeviceMidi` は、USB-MIDI 1.0 の Function です。MIDI の1イベント（4 byte の USB-MIDI event packet）単位で送受信します。
各 OS に標準のドライバがあり、DAW や音楽アプリから MIDI ポートとして見えます。

使う場面の目安です。

- MIDI コントローラ（鍵盤、つまみ、パッド）を作る。
- Host の音楽アプリから MIDI を受けて、音源や照明を動かす。
- 1つの interface に最大 16本の仮想 cable（MIDI ポート）を持てます。複数の MIDI Function も登録できます。

MIDI 2.0（UMP）は対象外です。

動作は、ESP32-S3 同士の FS 接続と、ESP32-P4/P4X の HS 直結（どちらも EspUsbHost を Host にした試験）で確認しています。各 OS の音楽アプリとの相互接続は未確認です。

## 基本の使い方

```cpp
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceMidi midi(device);  // cable 1本（送受信とも）

void setup() {
  EspUsbMidiConfig config = midi.configuration();  // constructor の cable 数を引き継ぐ
  config.name = "ESP MIDI";
  if (midi.configure(config) != ESP_OK) return;
  device.begin();
}

void loop() {
  EspUsbDeviceMidiPacket packet;
  while (midi.readPacket(packet)) {
    // 受け取った1イベントを処理する
  }
  midi.noteOn(0, 60, 100);   // channel 0（0〜15）、note 60、velocity 100、cable 0
  delay(500);
  midi.noteOff(0, 60, 0);
  delay(500);
}
```

方向の名前は USB の慣例どおり Host から見たものです。**IN＝Device→Host＝このデバイスが送る**、**OUT＝Host→Device＝このデバイスが受ける**。
MIDI class の規格書は embedded jack を Device 側から見て名付けているので、逆向きになる点に注意してください。

## 設定

constructor は2つの形があります。

```cpp
EspUsbDeviceMidi midi(device, 2);       // 送受信とも cable 2本
EspUsbDeviceMidi midi(device, 4, 1);    // 送信（IN）4本、受信（OUT）1本
```

`EspUsbMidiConfig` の全項目です。

| 項目 | 既定値 | 範囲・意味 | FS/HS の違い |
| --- | --- | --- | --- |
| `inCables` / `outCables` | 0 / 0（今の数を保つ） | 送信側・受信側の cable 数。各 1〜16。0 は constructor（または直前）の数を保つ | なし |
| `name` | `nullptr` | Function 名（UTF-8）。begin でコピーし、interface 名として出す | なし |
| `rxPackets` | 128 | 受信 ring の容量（イベント数。1イベント＝4 byte）。`Queue` 以外では 0 | なし |
| `txPackets` | 128 | 送信 ring の容量（イベント数）。0 なら送信しない | なし |
| `transferPackets` | 0（自動） | 1回の送信転送に載せる最大イベント数。0 は bus packet 1個分。上限 16383 | 自動の値は FS 16イベント（64 byte）、HS 128イベント（512 byte） |
| `rxMode` | `Queue` | 受信方法。`Queue` / `Callback` / `Disabled` | なし |
| `autoFlush` | `true` | `false` にすると `flush()` を呼ぶまで送信を始めない | なし |
| `streamTermination` | `FunctionDefault` | MIDI の既定は `EndWithShortPacket`（送信が MPS の倍数で終わり、ring が空なら ZLP を送る） | ZLP が付く長さは FS 64、HS 512 の倍数 |
| `rxBuffer` / `txBuffer` | `nullptr` | ring 用の配列（`EspUsbDeviceMidiPacket`）を自分で渡す。`nullptr` なら begin で確保 | なし |

- `configure()` はすべての項目を置き換えます。ただし cable 数の 0（既定）は「今の数を保つ」で、constructor か直前の `configure()` の値が残ります。
- `configure()` と `onPacket()` は停止中だけ成功します。動作中は `ESP_ERR_INVALID_STATE` を返し、元の設定を保ちます。
- 設定を変えるときは `device.end()` → `configure()` → `device.begin()` の順に呼びます。
- `configuration()` は要求した値をそのまま返します。`transferPackets = 0` も 0 のままです。

`configure()` の戻り値：

| 戻り値 | 条件 |
| --- | --- |
| `ESP_OK` | 受け付けた |
| `ESP_ERR_INVALID_STATE` | 動作中 |
| `ESP_ERR_INVALID_ARG` | cable 数が 0 か 17 以上、`rxMode` が範囲外、`Callback` / `Disabled` なのに `rxPackets` か `rxBuffer` を指定、容量が大きすぎる、`rxBuffer` と `txBuffer` が重なる |
| `ESP_ERR_INVALID_SIZE` | `transferPackets` が 16383 を超える（1回の転送長が 16 bit に収まらない） |

cable 数は丸めずに拒否します。

begin の後で MIDI が動いていないときは、`midi.startError()` で理由が分かります。`rxMode = Callback` なのに `onPacket()` が未登録なら `ESP_ERR_INVALID_ARG`、メモリ不足は `ESP_ERR_NO_MEM` です。

## API

### 送信

| 関数 | 戻り値 |
| --- | --- |
| `writePacket(packet)` | イベントを送信 ring へコピーできたら true |
| `noteOn` / `noteOff` / `controlChange` / `programChange` / `polyPressure` / `channelPressure` / `pitchBend` | 同上。最後の引数 `cable` は 0 始まりで既定 0 |
| `flush()` | 溜まったイベントの送信を始める（`autoFlush = false` のとき） |
| `availableForWritePackets()` | 今すぐ書けるイベント数 |
| `sendCapacity()` | 確保した送信 ring のイベント数（停止中は 0） |
| `transferCapacity()` | 1回の送信転送の実際の容量（イベント数、停止中は 0） |

- 送信は 4 byte 単位です。ring に空きがなければ false を返し、イベントの一部だけを受け付けることはありません。
- true は「ring へコピーした」という意味です。Host が受け取ったことは意味しません。
- 複数の task から同時に送っても、イベント同士の byte が混ざることはありません。
- 送信先の cable が `inCables` 以上のとき、または CIN（Code Index Number、header の下位 4 bit）が予約値の 0 か 1 のときは false を返します。Host に知らせていない cable へ黙って送ることはありません。
- 便利関数が受け付けるのは、channel 0〜15、note や値 0〜127（7 bit）です。`pitchBend` の値は 0〜16383（中央は 8192）です。範囲外の値は丸めません。false を返し（`device.lastError()` は `ESP_ERR_INVALID_ARG`）、何も送りません。丸めると別のメッセージになるからです（MIDI 1.0 の data byte は 7 bit）。
- SysEx やリアルタイムメッセージは、規格どおりの CIN（0x4〜0x7、0xF など）で `EspUsbDeviceMidiPacket` を組み立てて `writePacket()` で送ります。
- 送信関数は待ちません。USB worker の callback からも呼べます。

### 受信

| 関数 | 戻り値 |
| --- | --- |
| `readPacket(packet)` | 1イベント読めたら true |
| `availablePackets()` | 今読めるイベント数 |
| `receiveCapacity()` | 確保した受信 ring のイベント数（停止中は 0） |
| `receiveErrors()` | 捨てた不正イベントと端数の累計。begin で 0 に戻る |
| `onPacket(callback)` | `rxMode = Callback` 用の通知先。停止中だけ `ESP_OK` |

- 受信した USB packet のうち、cable が `outCables` 以上のもの、CIN が 0 か 1 のもの、4 byte に満たない端数は捨て、`receiveErrors()` を増やします。同じ packet 内の正しいイベントは順番どおりに渡します。
- MIDI メッセージや SysEx の中身は検査しません。正しいイベントの中身は変えません。
- 受信 ring より大きな USB packet が届いた場合、残りは DMA 用の領域に保持し、ring が空くたびに移します。全部読み終えるまで次の packet は受けません（Host には NAK を返して待たせます）。このため ring 容量が 1 でもイベントを失いません。

### callback で受ける

`rxMode = Callback`、`rxPackets = 0` にして、停止中に `onPacket()` を登録します。

```cpp
EspUsbMidiConfig config = midi.configuration();
config.rxMode = EspUsbMidiRxMode::Callback;
config.rxPackets = 0;
midi.configure(config);
midi.onPacket([](const EspUsbDeviceMidiPacket &packet) {
  // USB worker で呼ばれる。packet はこの呼び出しの間だけ有効
});
```

- callback には1イベントの const 参照が渡ります。後で使うなら 4 byte をコピーします。
- callback は USB worker で動きます。待つ処理、`device.end()`、設定変更はしないでください。
- callback の中の `writePacket()` も待たずに戻ります。空きがなければ false です。

### 片方向だけ使う

- 送信専用：`rxMode = Disabled`、`rxPackets = 0`。受信 ring と受信用の DMA 領域を確保しません。
- 受信専用：`txPackets = 0`。送信側を確保しません。
- descriptor には bulk IN/OUT の両方が載るので、endpoint 数は両方向分が必要です。

## FS と HS の違い

| 項目 | FS（S3、P4 の FS controller） | HS（P4/P4X の HS controller） |
| --- | --- | --- |
| bulk の MPS | 64 byte | 512 byte |
| `transferPackets = 0` の実際の値 | 16イベント | 128イベント |
| `transferPackets` に MPS 未満の値 | 受け付ける | 受け付ける |

- 自動の値は、選んだ controller が HS に対応するかで決まります（P4 で `controller` が `FullSpeed` 以外なら HS の値）。
- MIDI 1つで非 control の IN endpoint を1本使います。cable を増やしても endpoint は増えません（cable は packet header の番号で区別します）。

## バッファとメモリ

| 領域 | 置ける場所 |
| --- | --- |
| `rxBuffer` / `txBuffer`（ring） | 内部 RAM でも PSRAM でもよい。DMA を通らない |
| 自動確保の ring | 通常の `malloc` で確保 |
| DMA 用の packet 領域 | ライブラリが DMA 可能な内部 RAM に確保 |

- 渡した配列は、容量（イベント数×4 byte）分を begin から end まで有効に保ってください。ライブラリは解放も拡張もしません。
- PSRAM 上の ring は Vendor で確認済みです。MIDI の ring を PSRAM に置く構成は実機で未確認です。
- MIDI を使わないプログラムには、MIDI の実装はリンクされません。

## Host OS 側の扱い

USB MIDI 1.0 は各 OS の標準ドライバで動きます。下の内容は各 OS の一般的な仕組みです。この版のライブラリでの PC との相互接続は未確認です。

| OS | 標準ドライバ | 見え方 |
| --- | --- | --- |
| Windows | usbaudio.sys（USB Audio 1.0 の MIDI） | MIDI 入出力ポート |
| Linux | snd-usb-audio | ALSA の rawmidi / sequencer のポート（`aconnect -l`、`amidi -l` で確認） |
| macOS | CoreMIDI の標準ドライバ | 「Audio MIDI 設定」の MIDI スタジオに出る |

- cable ごとに MIDI ポートが1つ見えます。ポート名の付け方は OS とアプリで違います。`name` は interface 名として出しますが、cable ごとの名前（jack の iJack）は出していません。
- MIDI の interface は IAD なしの Audio Control＋MIDI Streaming の組です（MIDI 1.0 の形式）。他の Function が IAD を使う複合装置で、各 OS がどうまとめるかは未確認です。
- 1つの OS アプリが2つ目の MIDI Function を扱えるかはアプリ次第です。EspUsbHost の標準 MIDI API は最初の interface だけを扱います。
- descriptor の長さは MIDI 部分だけで 60＋16×（送信 cable 数＋受信 cable 数）byte です（cable 1/1 で 92 byte、16/16 で 572 byte）。configuration descriptor 全体を受け取れる長さに上限がある Host では、cable を増やすと列挙に失敗します。

## 規格との対応と設計の理由

- **descriptor**：USB-MIDI 1.0 の形式に従い、Audio Control interface（subclass 1、header 1.00）と MIDI Streaming interface（subclass 3、header 1.00）を出します。cable ごとに embedded jack と external jack を組にし、bulk endpoint は 9 byte の標準 endpoint descriptor と class 固有の MS endpoint descriptor（associated jack の一覧）を持ちます。
- **cable の上限 16**：USB-MIDI 1.0 の event packet の header で、cable 番号が 4 bit だからです（USB-MIDI 1.0 §4）。
- **CIN 0 と 1 の拒否**：CIN 0x0 と 0x1 は将来の拡張のための予約です（USB-MIDI 1.0 §4 の表 4-1）。
- **イベント単位の API にした理由**：1イベントが分割されないことを型で保証するためです。MIDI は `Print` でも `Stream` でもありません。byte 単位の `write()` / `read()` は公開しておらず、誤った型変換はコンパイル時に拒否されます。
- **runtime 構成の利点**：ビルド設定なしで再構成できる、MIDI Function を複数持てる、方向ごとに資源を省ける、小さな queue で動く。代わりに begin で動的確保が起き、不正な cable/CIN を拒否する分だけ移行の手間があります。

## v2 からの変更点

- `CFG_TUD_MIDI` と `CFG_TUD_MIDI_*` は廃止しました。容量は `EspUsbMidiConfig` で指定します。
- TinyUSB の `tud_midi_*` を直接呼んでいたコードは、`readPacket` / `writePacket` などへ移してください。
- cable 数の 0 や 17 以上は拒否されます。
- `readPacket` / `writePacket` / `noteOn` などの呼び方は v2 と同じです。

詳しくは [v2 からの移行](../migrating-from-v2.ja.md) を参照してください。

## 関連する examples

| example | 内容 |
| --- | --- |
| [MIDI](../../examples/MIDI/) | note と control change の送信、受信のログ |
| [MIDIController](../../examples/MIDIController/) | ADC とボタンの入力を MIDI メッセージにする |
| [MIDIInterface](../../examples/MIDIInterface/) | UART（DIN）MIDI と USB MIDI の中継 |

規格：[USB Device Class Definition for MIDI Devices 1.0](https://www.usb.org/sites/default/files/midi10.pdf)
