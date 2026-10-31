# CDC ACM（仮想シリアルポート）

[English](cdc.md)

## 概要と使う場面

`EspUsbDeviceCdcSerial` は、Host から仮想 COM ポート（USB CDC の ACM model）として見える Function です。
Arduino の `Stream` を実装しているので、`Serial` と同じように `print()` / `read()` / `readBytes()` で使えます。

使う場面の目安です。

- serial terminal や、既存の serial 用アプリから使いたい。
- ログやコマンドのやり取り。
- 独自の protocol で速度が必要なら [Vendor](vendor.ja.md)、ドライバなしで小さなデータを送るなら [HID の Vendor](hid.ja.md) を使います。

1つの装置に複数の CDC を登録できます（endpoint 数の範囲内。[FS と HS の違い](#fs-と-hs-の違い)）。

送受信の仕組み（ring、DMA、借用転送、取消し、標準要求による初期化）は Vendor と共通です。共通部分の詳細は [Vendor](vendor.ja.md) を参照してください。この文書では CDC で違う点を中心に説明します。

動作は、ESP32-S3 同士の FS 接続、ESP32-P4 一台の FS loopback、旧 P4 と P4X の HS 直結（両方向）で確認しています。PC の OS との相互接続は未確認です。

## 基本の使い方

```cpp
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceCdcSerial console(device, "Console");  // 第2引数は interface 名（省略可）

void setup() {
  EspUsbBulkStreamConfig config;   // 設定しなければ既定値（RX/TX とも 512 byte）
  config.rxCapacity = 1024;
  config.txCapacity = 2048;
  if (console.configure(config) != ESP_OK) return;  // 停止中だけ成功する
  if (!device.begin()) return;  // 失敗理由は device.lastError()
}

void loop() {
  while (console.available()) {
    const int value = console.read();
    if (value >= 0) console.write(uint8_t(value));  // echo
  }
}
```

## 設定

設定の型は Vendor と同じ `EspUsbBulkStreamConfig` です。各項目の意味と規則は [Vendor の設定](vendor.ja.md#設定) と同じで、CDC で既定の意味が変わる項目に印を付けています。

| 項目 | 既定値 | 範囲・意味 | FS/HS の違い |
| --- | --- | --- | --- |
| `rxCapacity` | 512 | 受信 ring の容量（byte）。`rxMode = Stream` では 1 以上、それ以外では 0 | なし |
| `txCapacity` | 512 | 送信 ring の容量（byte）。0 なら `write()` はすぐ 0 を返す | なし |
| `rxBuffer` / `txBuffer` | `nullptr` | ring の領域を自分で渡すときに指定。PSRAM でもよい | なし |
| `transferBytes` | 512 | コピー送信で1回に DMA へ渡す最大 byte 数（1〜65535） | MPS 未満でも受け付ける |
| `autoFlush` | `true` | `write()` のたびに送信を始めるか | なし |
| `streamTermination` | `FunctionDefault` | **CDC では `EndWithShortPacket`**（MPS の倍数で終わり、ring が空なら ZLP を送る） | ZLP が付く長さは FS 64、HS 512 の倍数 |
| `rxMode` | `Stream` | `Stream` / `Callback` / `Borrowed` / `Disabled` | なし |

CDC 固有の設定：

| 項目 | 既定値 | 意味 | 変更できる時期 |
| --- | --- | --- | --- |
| constructor の `name` / `setName(name)` | `nullptr` | interface 名（UTF-8）。Host が CDC を見分けるのに使う | 停止中だけ。動作中は `ESP_ERR_INVALID_STATE` |
| `setTxTimeoutMs(ms)` | 1000 | `write()` / `flush()` が送信を待つ上限（ms）。0 なら待たない | いつでも。各呼び出しは開始時の値を使う |
| `setTimeout(ms)`（Stream） | 1000 | `readBytes()` などの読み出しを待つ上限（ms）。USB の転送とは無関係 | いつでも |

- `setName()` に渡した文字列は pointer として保持します。`device.begin()` が戻るまで有効にしておいてください。descriptor はその時点の写しを使います。
- `setTxTimeoutMs()` と `setTimeout()` の値は、`end()` → `begin()` の後も残ります。
- 設定の規則（停止中だけ `configure()` できる、begin で確保する、エラーの値）は [Vendor の規則](vendor.ja.md#設定と開始停止の規則) と同じです。

## API

### 状態と通知

| API | 説明 |
| --- | --- |
| `mounted()` | Host が構成を選び、endpoint が開いている |
| `connected()` | `mounted()` かつ DTR = 1（Host の terminal が開いている目安）。`write()` は DTR を条件にしない |
| `lineCoding()` | 今の baud、stop bit、parity、data bit の写し。初期値は 115200 / 0 / 0 / 8。値で返すので、最新値は呼び直して取る |
| `lineState()` | 今の DTR / RTS の写し。bus reset で両方 false に戻る |
| `onLineCoding(cb)` / `onLineState(cb)` / `onBreak(cb)` | Host からの変更の通知。停止中に登録する（動作中は `ESP_ERR_INVALID_STATE`） |
| `port()` | begin で決まる、CDC の中での並び順の番号 |
| `readCapacity()` / `writeCapacity()` | 実際に確保した容量。開始前と停止後は 0 |
| `configuration()` | 要求した設定（`FunctionDefault` などもそのまま） |

- 通知の callback は USB worker で呼ばれます。待つ処理、`device.end()`、設定変更はしないでください。
- `onBreak` には SEND_BREAK 要求の値（期間、ms）がそのまま渡ります。0 と 0xFFFF の意味は CDC 1.2 の PSTN 規格のとおりです。
- baud や parity は、Host のアプリが選んだ値を知らせるだけです。USB の転送速度には影響しません。UART へ中継する場合は、この値で UART を設定します。

### 受信（Arduino Stream）

CDC はそのまま `Stream&` へ渡せます。

```cpp
void consume(Stream &input) {
  input.setTimeout(25);
  uint8_t bytes[16];
  const size_t count = input.readBytes(bytes, sizeof(bytes));
}
consume(console);  // device.begin() の後、アプリの task で
```

| 関数 | 動作 |
| --- | --- |
| `available()` / `read()` / `read(buffer, size)` | 届いている分だけ返す。待たない |
| `peek()` | 先頭の 1 byte を消費せずに返す。空、または `rxMode` が `Stream` でなければ -1 |
| `readBytes()`、`find()`、`parseInt()` など | Stream の timeout（既定 1000 ms）まで待つ |

- 待つ関数は USB worker の callback から呼ばないでください。
- `readString()` などは Arduino の `String` を使うので、heap を使います。`read()` / `readBytes()` は使いません。
- reset や end の後に古い byte は残りません。

### 送信の期限と flush

CDC の `write()` と `flush()` は、Arduino の `Serial` に合わせて**期限付きで待ちます**。Vendor の `write()` / `flush()` は待たないので、ここが違います。

- `write()`：空きがあればすぐコピーして戻ります。足りないときは送信を進めながら期限まで待ち、受け付けた総数を返します。ring より大きいデータも期限内なら全部送れます。`print()` も同じです。
- 期限切れ、切断、bus reset、停止のときは、一部だけ、または 0 を返すことがあります。
- DTR は `write()` の条件ではありません。`connected()` が false でも、Host が構成を選んでいれば送れます。
- `flush()`：溜まったデータの送信を始め、DMA と必要な ZLP が終わるまで期限付きで待ちます。Host のアプリが読んだことまでは意味しません。期限が切れても未送信のデータは捨てず、Host が読み出しを再開すれば続きを送ります。
- `autoFlush = false` でも、`flush()` と、空き待ちに入った `write()` は送信を始めます。
- `txCapacity` が 0 なら、`write()` はすぐ 0 を返します。

呼び出す場所による違い：

| 呼び出し元 | `write()` | `flush()` |
| --- | --- | --- |
| 通常の task | 期限まで待つ | 期限まで待つ |
| USB worker の callback | 待たずに入る分だけ受け付ける | 送信を始めるだけ |
| 割込み（ISR） | 0 を返す | 何もしない |

worker の中で待つと、自分が処理するはずの完了を待ち続けてしまうためです。借用送信（`writeDirect`）にはこの待ちはありません。

### 空き容量の照会

`availableForWrite()`（`Print` の関数）は、今 `write()` が待たずに受け付ける byte 数を返します。`int` に収まらない値は `INT_MAX` です。`size_t` で欲しいときは `writeAvailable()` を使います。
借用送信中、停止中、TX ring がないとき、IN endpoint が HALT 中は 0 です。

### コピー送信の終端（ZLP）

CDC の既定は `EndWithShortPacket` です。コピー送信が MPS の倍数ちょうどで終わり、ring が空になったら、ZLP を送って Host の read を終わらせます。
Host が大きな単位で read していても、少量のデータがすぐ渡るようにするためです。

- 連続した stream で ZLP が要らなければ `Continue` を選べます。
- ZLP は「1回の `write()` がメッセージの区切りになる」という意味ではありません。複数の `write()` はまとめて送られることがあります。

### 借用転送

CDC でも `writeDirect` / `readDirect`、方向別の完了 callback、`cancelDirectTx` / `cancelDirectRx` を使えます。動作と結果は Vendor と同じです（[Vendor の借用転送](vendor.ja.md#借用転送)）。

- `rxMode = Borrowed` にし、`rxCapacity` / `txCapacity` / `transferBytes` を 0 にすれば、コピー用の ring は確保しません。
- 受信を使わないなら `rxMode = Disabled`、`rxCapacity = 0` にします。

## Host からの要求への応答

| 要求 | 動き |
| --- | --- |
| SET_LINE_CODING | 下の条件を満たすときだけ受け付け、status 段階の成功後に `lineCoding()` を更新して `onLineCoding` を呼ぶ。満たさなければ STALL |
| GET_LINE_CODING | 今の値を 7 byte で返す |
| SET_CONTROL_LINE_STATE | DTR / RTS を更新し、`onLineState` を呼ぶ |
| SEND_BREAK | `onBreak` を呼ぶ |
| SET_INTERFACE（data interface、alt 0） | data の送受信を初期化。借用中の転送は `Cancelled` で返り、ring は捨てる |
| SET_INTERFACE（control interface、alt 0） | 通知 endpoint だけを初期化。data 転送、DTR、line coding は保つ |
| SET_FEATURE / CLEAR_FEATURE(ENDPOINT_HALT) | Vendor と同じ（[標準要求による転送の初期化](vendor.ja.md#標準要求による転送の初期化)） |

SET_LINE_CODING を受け付ける条件：要求長（wLength）が 7 で、DATA 段階で実際に 7 byte を受け取り、値が次の範囲にあること。

| 項目 | 受け付ける値 |
| --- | --- |
| baud | 0 以外 |
| stopBits | 0〜2 |
| parity | 0〜4 |
| dataBits | 5〜8、16 |

## FS と HS の違い

| 項目 | FS（12 Mbps） | HS（480 Mbps） |
| --- | --- | --- |
| data の bulk MPS | 64 | 512 |
| `transferBytes` | 1〜65535。MPS 未満でもよい | 同左 |
| `readDirect()` の容量 | 64 の倍数 | 512 の倍数 |
| ZLP が付く長さ | 64 の倍数 | 512 の倍数 |
| 通知 endpoint（interrupt IN） | 8 byte、bInterval 1（1 ms） | 8 byte、bInterval 1（125 µs） |

- CDC 1つで非 control の IN endpoint を2本（通知と data）使います。上限は ESP32-S3 で 4本、P4 の HS controller で 7本です。S3 で CDC を2つ使うと、IN endpoint を使う他の Function は置けません。
- CDC は serial-state の通知を送りません。通知 endpoint は、ACM の descriptor に必要なので置いています。

## バッファとメモリ

Vendor と同じです（[Vendor のバッファとメモリ](vendor.ja.md#バッファとメモリ)）。

- `rxBuffer` / `txBuffer` の ring は PSRAM に置けます。DMA 用 buffer はライブラリが内部 RAM に確保します。
- 借用転送の buffer は DMA 可能な内部 RAM が必要です。PSRAM は `NotDmaCapable` になります。
- CDC 固有に、line coding 用の DMA 領域 64 byte を begin で確保し、end で解放します。
- PSRAM の ring を実機で確かめたのは Vendor です。CDC は同じ転送層を使いますが、CDC としては未確認です。

## Host OS 側の扱い

CDC ACM は各 OS の標準ドライバで動きます。下の内容は各 OS の一般的な仕組みです。この版のライブラリでの PC との相互接続は未確認です。

| OS | 標準ドライバ | 見え方 |
| --- | --- | --- |
| Windows 10 以降 | usbser.sys（INF 不要） | COM ポート |
| Linux | cdc_acm | `/dev/ttyACM*` |
| macOS | AppleUSBCDC | `/dev/cu.usbmodem*` |

- 装置の descriptor は、IAD があるとき class 0xEF / 0x02 / 0x01（複合装置）を宣言します。CDC は必ず IAD を出すので、Windows は usbccgp を親として読み込み、CDC ごとに COM ポートを作ります。
- 複数の CDC を見分けるには、それぞれに名前を付けます（`EspUsbDeviceCdcSerial(device, "Console")`）。Windows は IAD の iFunction の文字列を子装置の名前に使います。
- Windows は COM ポートの番号を VID/PID/serial と interface 番号ごとに覚えます。基板ごとに異なる `EspUsbDeviceConfig::serialNumber` を設定すると、USB ポートを替えても番号が変わりません。
- Linux で一般ユーザーが開くには、`dialout` グループなどの権限が要ります。ModemManager が新しい ACM 装置に接続を試みることがあります。
- 上の Windows の振る舞いは v2 で同じ形式の descriptor を使って確認したものです。v3 では Windows 実機で未確認です。macOS も未確認です。

## 規格との対応と設計の理由

- **descriptor**：CDC 1.2 の ACM model（subclass 0x02）に従い、Header、Call Management、ACM（bmCapabilities 0x06：line coding と SEND_BREAK）、Union の functional descriptor と、通知用の interrupt IN、data interface（class 0x0A）の bulk IN/OUT を出します。
- **Stream を CDC だけに実装した理由**：CDC は serial の代わりに使われるので、`Serial` と同じ型で渡せることに価値があります。Vendor は `Print` だけ、MIDI は event 単位の API です。
- **期限付きの待ちを CDC だけに入れた理由**：Arduino の `Serial` と同じ感覚で使えるようにするためです。待つための task や同期 object は追加していません。
- **既定の ZLP 終端**：Host が大きな read で待っていても、少量のデータが届くようにするためです（USB 2.0 §5.8.3）。
- **SET_LINE_CODING の検査**：範囲外の値を受け入れると、UART に中継するアプリが不正な設定を受け取るためです。STALL で Host に失敗を伝えます。

## v2 からの変更点

- `CFG_TUD_CDC` や `CFG_TUD_CDC_*_BUFSIZE` で個数や容量を決める方法は使えません。容量は `configure()` で指定します。
- `onLineCoding` / `onLineState` / `onRx` / `setName` は `esp_err_t` を返し、停止中だけ設定できます。
- `lineCoding()` / `lineState()` は参照ではなく値を返します。
- `Device::maxCdcPorts()` は廃止しました。構成全体を begin で検証するので、失敗したら `device.lastError()` を確認してください。
- `write()` / `flush()` は期限付きで待ちます（[送信の期限と flush](#送信の期限と-flush)）。

詳しくは [v2 からの移行](../migrating-from-v2.ja.md) を参照してください。

## 関連する examples

| example | 内容 |
| --- | --- |
| [Serial](../../examples/Serial/) | 1つの CDC で echo |
| [SerialMulti](../../examples/SerialMulti/) | 名前付きの CDC を2つ |
| [FirmwareCDC](../../examples/FirmwareCDC/) | CDC でのファームウェア更新 |
| [CompositeHidCdcMsc](../../examples/CompositeHidCdcMsc/) | HID と CDC、MSC の複合 |

規格：[USB CDC 1.2](https://www.usb.org/document-library/class-definitions-communication-devices-12)（CDC と PSTN の文書）
