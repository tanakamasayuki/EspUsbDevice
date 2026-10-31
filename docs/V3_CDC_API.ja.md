# CDC API（v3）

`EspUsbDeviceCdcSerial` は、Hostから仮想COMポート（CDC-ACM）として見えるFunctionです。
Arduinoの`Stream`を実装しているので、`Serial`と同じように`print()`/`read()`で使えます。
送受信の仕組み（ring、DMA、借用転送、取消し）はVendorと共通です。共通部分の詳細は[Vendor API](V3_VENDOR_API.ja.md)を参照してください。

用語（MPS、ring、借用転送、ZLP）は[Vendor API](V3_VENDOR_API.ja.md)の冒頭と同じ意味です。

## 基本の使い方

```cpp
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceCdcSerial console(device, "console");  // 第2引数はinterface名（省略可）

void setup() {
  EspUsbBulkStreamConfig config;   // 設定しなければ既定値（RX/TXとも512 byte）
  config.rxCapacity = 1024;
  config.txCapacity = 2048;
  if (console.configure(config) != ESP_OK) return;  // 停止中だけ成功する
  if (!device.begin()) return;  // 失敗理由は device.lastError()
}

void loop() {
  if (console.available()) {
    const int value = console.read();
    if (value >= 0) console.write(uint8_t(value));
  }
}
```

### 主な設定

設定の型はVendorと同じ`EspUsbBulkStreamConfig`です。CDCで既定値の意味が変わる項目に印を付けています。

| 項目 | 既定値 | 意味 | FS/HSでの違い |
| --- | --- | --- | --- |
| `rxCapacity` | 512 | 受信ringの容量（byte）。`rxMode=Stream`では1以上、それ以外では0 | なし |
| `txCapacity` | 512 | 送信ringの容量（byte）。0なら`write()`はすぐ0を返す | なし |
| `rxBuffer` / `txBuffer` | `nullptr` | ringの領域を自分で渡すときに指定。PSRAMでもよい | なし |
| `transferBytes` | 512 | コピー送信で1回にDMAへ渡す最大byte数（1〜65535） | MPS未満でも受け付ける（FS・HSとも） |
| `autoFlush` | `true` | `write()`のたびに送信を始めるか | なし |
| `streamTermination` | `FunctionDefault` | **CDCでは`EndWithShortPacket`**（必要ならZLPで区切る） | ZLPが付く長さがFS 64、HS 512の倍数 |
| `rxMode` | `Stream` | `Stream`/`Callback`/`Borrowed`/`Disabled` | なし |

CDC固有の設定：

| 項目 | 既定値 | 意味 |
| --- | --- | --- |
| `setTxTimeoutMs(ms)` | 1000 | `write()`/`flush()`が送信を待つ上限。0なら待たない。稼働中も変更できる |
| `setTimeout(ms)`（Stream） | 1000 | `readBytes()`などの読出し待ちの上限。USBの転送とは無関係 |
| `setName(name)` | コンストラクタの値（`nullptr`可） | interface名（UTF-8）。停止中だけ変更できる |

設定の規則（停止中だけ`configure()`できる、`begin()`で確保する、エラーの値）はVendorと同じです。[Vendorの「設定と開始・停止」](V3_VENDOR_API.ja.md#設定と開始停止)を参照してください。

## 主なAPI

| API | 説明 |
| --- | --- |
| `mounted()` | Hostが構成を選び、endpointが開いている |
| `connected()` | `mounted()`かつDTR=1（Hostのterminalが開いている目安）。`write()`はDTRを条件にしない |
| `lineCoding()` | 現在のbaud、stop bit、parity、data bitの写し。値で返すので、最新値は呼び直して取る。初期値は115200/0/0/8 |
| `lineState()` | 現在のDTR/RTSの写し。bus resetで両方falseに戻る |
| `onLineCoding(cb)` / `onLineState(cb)` / `onBreak(cb)` | Hostからの設定変更の通知。停止中に登録する。USB workerで呼ばれるので、待つ処理・`end()`・設定変更はしない |
| `port()` | `begin()`で決まる、CDCの記述子の並び順の番号 |
| `readCapacity()` / `writeCapacity()` | 実際に確保した容量。開始前と停止後は0 |
| `configuration()` | 要求した設定（`FunctionDefault`などもそのまま） |

`onBreak`には、HostのSEND_BREAK要求の値（期間、ms）がそのまま渡ります。0や0xFFFFの意味はUSB CDCの規格どおりです。

`setName()`に渡した文字列は、pointerとして保持されます。`begin()`が戻るまで有効にしておいてください（記述子はその時点の写しを使います）。

## Arduino Streamとして使う

CDCはそのまま`Stream&`へ渡せます。変換用のobjectは要りません。

```cpp
void consume(Stream &input) {
  input.setTimeout(25);
  uint8_t bytes[16];
  const size_t count = input.readBytes(bytes, sizeof(bytes));
  // countは受け取った数。readBytesは残りを待つことがある
}
// device.begin()後、Hostが構成を選んでから、アプリのtaskで呼ぶ
consume(console);
```

- `read(buffer, size)`は届いている分だけ返し、待ちません。
- `readBytes()`/`find()`/`parseInt()`などはStreamのtimeout（既定1000 ms）まで待ちます。USB workerのcallbackからは呼ばないでください。
- `peek()`は先頭の1 byte（0〜255）を消費せずに返します。空、または`rxMode`が`Stream`でなければ-1です。reset/end後に古いbyteは残りません。
- `readString()`などを使うとArduinoの`String`の費用がかかります。`read()`/`readBytes()`は`String`を使いません。
- `setTimeout()`の値はobjectに残り、`end()`→`begin()`でも変わりません。

## CDC送信の期限とflush

CDCの`write()`と`flush()`は、Arduinoの`Serial`に合わせて**期限付きで待ちます**。Vendorや共通bulkの`write()`/`flush()`は待たないので、ここが違います。

- `setTxTimeoutMs(ms)`で待ちの上限を決めます。既定1000 ms、0なら待ちません。`txTimeoutMs()`で今の値を読めます。稼働中も変更でき、各呼出しは開始時の値を使います。`end()`→`begin()`でも値は残ります。
- `write()`：空きがあればすぐコピーして戻ります。空きが足りないときは、送信を進めながら期限まで待ち、受け付けた総数を返します。ringより大きいデータも、期限内なら全部送れます。`print()`も同じ経路です。
- 期限切れ、切断、bus reset、停止のときは一部だけ、または0を返すことがあります。全部届くことは保証しません。
- DTRは`write()`の条件ではありません。`connected()`がfalseでも、Hostが構成を選んでいれば送れます。
- `flush()`：溜まったデータの送信を始め、DMAと必要なZLPが終わるまで期限付きで待ちます。Host側のアプリが読んだことまでは意味しません。
- `flush()`は`void`なので、期限切れは戻り値で分かりません。期限が切れても未送信のデータは捨てず、Hostが読み出しを再開すれば続きを送ります。
- `autoFlush=false`でも、`flush()`と、空き待ちに入った`write()`は送信を始めます。期限0の`write()`だけなら、`autoFlush=false`のとおり溜めたままです。
- `txCapacity`が0なら、期限に関係なく`write()`はすぐ0を返します。

呼び出す場所による違い：

| 呼出し元 | `write()` | `flush()` |
| --- | --- | --- |
| 通常のtask | 期限まで待つ | 期限まで待つ |
| USB workerのcallback | 待たずに入る分だけ受け付ける | 送信を始めるだけ |
| 割込み（ISR） | 0を返す | 何もしない |

workerの中で待つと、自分が処理するはずの完了を待ち続けてしまうためです。
借用送信（`writeDirect`）にはこの待ちはありません。

## Arduino Printからの空き容量照会

CDCとVendorは`Print&`として渡せます。`availableForWrite()`は、今`write()`が受け付けられるbyte数を返します。
戻り値の型は`int`なので、大きな値は`INT_MAX`になります。`size_t`で欲しいときは`writeAvailable()`を使います。

```cpp
Print &output = console;
if (output.availableForWrite() > 0) output.write(uint8_t('!'));
```

- 借用送信中、停止中、TX ringなし、IN endpointがHALT中は0です。
- 照会しても領域は予約されません。`write()`の戻り値も確認してください。
- MIDIは`Print`/`Stream`ではありません（4 byteのevent単位のAPIです）。

## コピー送信の終端（ZLP）

`streamTermination`の既定`FunctionDefault`は、CDCでは`EndWithShortPacket`です。
コピー送信がMPSの倍数ちょうどで終わり、ringが空になったら、ZLPを送ってHostのreadを終わらせます。
Hostが大きな単位でreadしているときも、少量のserial dataがすぐ渡るようにするためです。

- 連続したstreamでZLPが要らなければ`Continue`を選べます。
- ZLPは「1回の`write()`がメッセージの区切りになる」という意味ではありません。複数の`write()`はまとめて送られることがあります。
- ZLPの送信中に次の送信が割り込むことはありません。コピー送信の完了通知（`onTxComplete`）に0 byteの通知は加えません。
- 借用送信の`EspUsbTransferTermination`は転送ごとの指定で、この設定とは別です。

## 借用転送と送信専用構成

CDCでも`writeDirect`/`readDirect`、方向別の完了callback、`cancelDirectTx`/`cancelDirectRx`を使えます。動作と結果はVendorと同じです（[Vendorの借用転送](V3_VENDOR_API.ja.md#借用転送コピーなし)）。

- `rxMode=Borrowed`にし、`rxCapacity`/`txCapacity`/`transferBytes`を0にすれば、コピー用のringやpacket bufferは確保しません。
- 受信を使わないなら`rxMode=Disabled`、`rxCapacity=0`にします（[受信の方式](V3_VENDOR_API.ja.md#受信の方式)）。
- CDC固有に、line coding用のDMA領域64 byteを`begin()`で確保し、`end()`で解放します。

## Hostによるinterface/endpointの初期化

bulk dataのSET_INTERFACE、SET_FEATURE/CLEAR_FEATURE（ENDPOINT_HALT）は、Vendorと同じ回復処理です（[標準要求による転送の再初期化](V3_VENDOR_API.ja.md#標準要求による転送の再初期化)）。

- data interfaceの再選択：借用中のTX/RXは`Cancelled`で返り、コピー用のqueue/ringは捨てられます。
- control interfaceの再選択：通知endpointだけを初期化します。data転送、DTR、line codingは保たれます。
- HALT中のINへの`write()`/`writeAvailable()`は0です。

## SET_LINE_CODINGの受理条件

Hostの要求長（wLength）が7 byteで、DATA段階で実際に7 byteを受け取り、値が次の範囲にあるときだけ受け付けます。

| 項目 | 受け付ける値 |
| --- | --- |
| baud | 0以外 |
| stopBits | 0〜2 |
| parity | 0〜4 |
| dataBits | 5〜8、16 |

条件を満たさなければSTALLし、`lineCoding()`も`onLineCoding`も変わりません。値の反映と通知は、status段階が成功した後に行います。

## FSとHSの違い

| 項目 | FS（12 Mbps） | HS（480 Mbps） |
| --- | --- | --- |
| data bulk MPS | 64 | 512 |
| `transferBytes` | 1〜65535。MPS未満でもよい | 同左 |
| `readDirect()`の容量 | 64の倍数 | 512の倍数 |
| ZLPが付く長さ | 64の倍数 | 512の倍数 |
| 通知endpoint（interrupt IN） | 8 byte、bInterval 1（1 ms） | 8 byte、bInterval 1（125 µs） |

- CDC 1つで非control INのendpointを2本（通知とdata）使います。上限はS3が4本、P4のHS controllerが7本です。S3でCDC 2つを使うと、他にIN endpointを使うFunctionは置けません。
- 現在のCDCはserial-stateの通知を送りません。通知endpointは記述子に必要なため置いています。

## PSRAMとバッファ

Vendorと同じです（[VendorのPSRAMとバッファ](V3_VENDOR_API.ja.md#psramとバッファ)）。
`rxBuffer`/`txBuffer`のringはPSRAMに置けます。DMA用bufferとline coding用の領域はライブラリが内部RAMに確保します。借用転送のbufferはDMA可能な内部RAMが必要で、PSRAMは`NotDmaCapable`になります。
なお、PSRAMのringを実機で確認したのはVendorです。CDCは同じ転送層を使いますが、CDCとしての確認記録はありません。

## 2.xからの移行

- `CFG_TUD_CDC`や`CFG_TUD_CDC_*_BUFSIZE`で個数や容量を決める方法は使えません。容量は`configure()`で指定します。TinyUSBのCDCは使いません。
- `onLineCoding`/`onLineState`/`onRx`/`setName`は`esp_err_t`を返します。稼働中に呼ぶとエラーになるので、開始前に移してください。
- `lineCoding()`/`lineState()`は参照ではなく値を返します。後から値が変わることを前提にせず、呼び直してください。
- `Device::maxCdcPorts()`は廃止しました。個数だけでは他のFunctionとのendpointの取り合いを表せないためです。構成全体を`begin()`で検証するので、失敗したら`device.lastError()`を確認してください。
- 2026-10-30から、CDCの`write()`/`flush()`は期限付きで待ちます（[CDC送信の期限とflush](#cdc送信の期限とflush)）。

## 設計判断の理由

- **CDCだけを`Stream`にする**：共通の転送層から`Print`を外し、CDCが`Stream`、Vendorが`Print`を実装します。MIDIはpacket単位のAPIなので、byte用のparserやtimeoutの状態を持ち込みません（[採用のための最小比較](#採用のための最小比較)）。
- **期限付きの待ちをCDCだけに入れる**：Arduinoの`Serial`と同じ感覚で使えるようにするためです。CDCのobjectにtimeoutと停止の世代番号だけを持たせ、新しいring、待ち用のtask、同期objectは確保しません。
- **既定のZLP終端**：Hostが大きなreadで待っていても、少量のdataが届くようにするためです。Vendorは従来どおり`Continue`です。
- **`availableForWrite()`のoverride**：以前は継承した`Print`の既定実装が常に0を返し、標準APIだけを使うコードが送信を始められませんでした。追加のbufferやtaskなしで実際の空きを返します。
- **得たもの**：実行時の設定、未使用資源の削減、Vendorと同じ所有権とエラーの規則、callback登録時期の統一。
- **払ったもの**：`begin()`での確保失敗、停止してからの再設定、借用bufferの寿命管理、値の写しを取り直す必要。

### 採用のための最小比較

Core 3.3.12/S3、同じ小さなCDC echoで比べました（v2との性能比較ではありません）。

| 案 | Flash増分 | 静的RAM増分 | 判断 |
| --- | --- | --- | --- |
| headerだけ含む未使用のadapter | 0 | 0 | 使わなければリンクされない |
| 使うときだけStream adapterを追加 | 524 byte | 24 byte | objectが20 byte。利用側に別のobjectが要る |
| 共通bulk baseをStreamにする | 412 byte | 8 byte | Vendor/MIDIまでStreamになるため不採用 |
| 共通転送層からPrintを分け、CDCだけStreamにする | 636 byte | 8 byte | 採用。CDCを直接Streamへ渡せる |

同じtoolchainでの`sizeof`は、CDC 224→232、Vendor 168→168、MIDI 208→200、共通bulk 152→144 byteです。
CDCのStream状態が8 byte増え、MIDIからは不要なPrintの状態が減りました。この数値はこのprobeでの記録で、他のアプリやSoCでの増分を保証しません。

## 実測の記録

記載時点の結果です。いまの仕様は上の節を見てください。

### HS直結（2026-10-31）

- `validation/peer/cdc_hid`：CDC ACMのecho（1〜4097 byte、line coding）とboot keyboardの複合構成をHS直結用に追加しました。
- 既存のS3 peer試験（`cdc_stream`、`cdc_tx`、`bulk_standard`などを含む39本）をP4/P4XのHS直結の両方向でも実行し、CDC関連の失敗はありませんでした（失敗はISO OUTの1件のみ）。
- 詳細は[P4/P4Xと試験環境](V3_P4_VARIANTS_AND_BENCH.ja.md#s3-peer試験をhs直結へ広げた結果2026-10-31)。

### TX待ちの実測（2026-10-30）

新しい`cdc_tx`の3件、既存の`cdc_stream`/`bulk_standard`/`audio_cdc`各1件、P4 loopback 6件の計12件が、標準combo allの1回で成功しました（374.48秒）。18のserial logはすべて正常停止で、panicやheap破損はありません。P4は両controllerのFS接続です。

- 64 byteのTX ringと64 byteのstagingで、`autoFlush`の有無とDTR=1/0を変え、4097 byteの`print()`を照合しました。
- Hostをbus suspendして送信を止め、期限0と60 ms、ringは空だがDMAだけ未完了の`flush()`、resume後の送信再開を確認しました。
- worker callbackからの`write()`は64 byteで即座に戻り、待ちの途中の`device.end()`は1000 msの期限より前に戻りました。
- `txCapacity`が0なら、1000 msの設定でもすぐ0を返しました。Hostが受け取った16900 byteはすべて一致しました。
- ISRからの呼出しは実機では試していません。
- 初回は、ringの空きとDMA stagingを同じ容量と見なした試験側の期待が誤っていて失敗しました。コピー送信はDMAへ移した時点でringが空くため、止まったHostに対してDMA＋ringの128 byteまで受け付けます。期待を直して成功しました。
- 記録は`validation/.pytest-results/cdc-tx-{initial,after,related}*`、`cdc-tx-audit.json`。
- S2のCdcConsoleもcompileできました。S2 buildのCDC objectは240 byteで、Stream化時の232 byteから8 byte増えています（timeoutと停止世代の分）。S2実機の結果ではありません（記録`cdc-tx-s2*`）。

### Print照会の修正前後

修正前は、CDCの実際の空きが64/128 byteなのにPrint経由の照会が0を返すことを再現しました（1 failed、51.79秒）。
修正後はCDC/Vendor/MIDIの3件が成功しました（337.64秒）。20回/10構成でPrintの値と実際の空きが一致し、Print経由の送信内容もHostで確認しました。
詳細は[validation記録](../validation/README.ja.md#printの空き容量の修正前後)。

### native Streamの関連検証

CDC/Vendor/MIDI、確保失敗、Function登録の5件が成功しました（465.84秒）。

- 1 byteのringを含む4つの容量構成で、Stream経由の空読み/timeout、繰り返しのpeek、readBytes、echoを確認。
- Vendorの非消費peekも1 byteのringで確認。Print経由の24照会/12構成も一致。
- 774か所の確保失敗からの回復を確認し、heapは346704→346704 byte。
- MIDIがPrint/Streamでないことをcompileで確認。
- 記録は`validation/.pytest-results/cdc-native-stream-related*`、比較のsource/nm/layoutは同じ場所の`stream-layout-*`。
- 続く製品`56a2c9a`固定の全34件も成功（2510.86秒）。[一括結果](../validation/README.ja.md#native-stream変更後の全34件)。

### 最初のS3実機試験

`validation/s3/cdc_stream`で、2つのCDCの容量を小/大/2のべき乗でない値へ同じbinaryのまま変え、両portで1/63/64/65/127/128/129 byte、line codingの独立性、借用RX/TXを確認しました。
S3の非control IN endpointは4本なので、CDC 2つと、CDC+Vendor+HIDは別の構成で試しました。後者ではHIDのキーの押下・解放も確認しています。
標準実行`cdc-vendor-all.xml`は6件成功（340.68秒）。CDCを含む46か所の確保失敗からの回復、取消し・reset/end・大きな転送・Vendorのコピー終端も含みます。
Deviceだけ/Vendorコピー/CDCコピーの各buildで、未使用Functionの処理、取消し、TinyUSBのCDC/Vendorがリンクされないことをnmで確認しました。

### SET_LINE_CODINGの未確認事項

2026-10-27のS3 Host SDKでは、wLength=7で送信量0〜6 byteの要求を作ると、`usb_host_transfer_submit_control`が`ESP_ERR_INVALID_ARG`を返し、送信されませんでした（[ESP-IDFのUSBH実装](https://github.com/espressif/esp-idf/blob/v5.5.1/components/usb/usbh.c#L232-L248)）。
このため、DATA段階が実際に短いOUTになる場合の実機確認はまだです。wLength自体が0〜6の要求の拒否とは別の話です。

### 未確認の範囲

- Windows/macOSでのCDCの相互接続
- serial-state通知の送信（未実装）
