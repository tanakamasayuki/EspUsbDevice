# v3 CDCと共通bulk stream API

2026-10-26。CDC-ACMのクラス処理を独自実装へ移し、TinyUSB CDCを無効化した。
VendorとCDCは`EspUsbDeviceBulkStream`のring、DMA、借用・返却、取消しを共有する。
MIDIも同じcopy転送を使うが、公開APIは4 byte event単位とし、byte streamや借用転送を直接使わせない。
共通EP0/coreとDWC2も本体内部で保守する。
現在はCDCをArduino Stream、VendorをPrintとして公開し、MIDIはどちらからも派生させない。
2026-10-30からCDCのcopy write/flushは期限付き待ちを行う。Vendorと共通bulkの非blocking操作とは区別する。開始modeとworker設定は[移行ガイド](V3_MIGRATION.ja.md)を参照。

## 普通の利用

```cpp
EspUsbDevice device;
EspUsbDeviceCdcSerial console(device, "console");

void setup() {
  EspUsbBulkStreamConfig config;
  config.rxCapacity = 1024;
  config.txCapacity = 2048;
  config.transferBytes = 256;
  if (console.configure(config) != ESP_OK) return;
  if (!device.begin()) return;
}

void loop() {
  if (console.available()) {
    const int value = console.read();
    if (value >= 0) console.write(uint8_t(value));
  }
}
```

DeviceとFunctionを作り、設定してbegin、read/write/Print、endという流れは維持する。
configureとcallback登録は停止中だけ成功する。容量はinstance単位で、end→configure→beginで変更する。
開始失敗時はdevice.lastError()を確認する。再開のためにbuild_opt.hやclean rebuildを使わない。
設定の意味とborrowed転送の返却契約は[Vendorガイド](V3_VENDOR_API.ja.md)と共通である。
copy RX/TXのringとtransferBytesは1 byteから指定でき、RXの一packetの残りを保持して小さいringでも欠落を防ぐ。
availableにはこの保留分も含む。DMA packetの容量とringの容量は別である。

| API/設定 | 意味 |
| --- | --- |
| EspUsbBulkStreamConfig / EspUsbBulkRxMode | Vendor/CDC共通の容量・外部ring・Stream/Callback/Borrowed設定 |
| readCapacity / writeCapacity | 確保済みの実効容量。開始前/終了後は0 |
| configuration | 要求した設定。FunctionDefaultなどの指定を保持 |
| mounted | USB configurationを受けてendpointが開いた状態 |
| connected | mountedかつDTR。writeはDTRを強制条件にしない |
| lineCoding / lineState | workerとの競合を避ける値のsnapshot。呼び直して最新値を取得 |
| onLineCoding / onLineState / onBreak | 停止中に登録。USB workerで通知し、block/end/構成変更を行わない |
| setName | 停止中にUTF-8を設定。各beginが戻るまで入力を保持し、descriptorはその時点のcopyを使う |
| port | beginで割り当てるCDC descriptor順のindex。TinyUSBのslotではない |

`EspUsbVendorConfig` / `EspUsbVendorRxMode`は共通型のaliasとして残す。
通常のVendor/CDC利用で共通baseを直接扱う必要はない。

## Arduino Streamとして使う

CDCをそのままStream&へ渡せる。adapterや別のserial objectは不要で、Device/config/beginと
従来のread/write/Print操作は維持する。共通bulk転送層自体からPrintを外し、CDCがStream、
VendorがPrintを実装する。packet APIを使うMIDIへbyte parserやtimeout stateを持ち込まない。

```cpp
void consume(Stream &input) {
  input.setTimeout(25);
  uint8_t bytes[16];
  const size_t count = input.readBytes(bytes, sizeof(bytes));
  // count is the number received; readBytes may wait for the rest.
}
// After device.begin()/Host configuration, on an application task:
consume(console);
```

- read(buffer, size)は現在ある分を返す非blocking操作のまま。
- readBytes/find/parseInt等はArduino Streamのtimeoutに従って待つ。USB worker callbackでは呼ばない。
  setTimeoutはapplication側の読出し待ちで、USBの転送timeoutやbegin configではない。
- peekは先頭の0..255、または空/非Stream RX modeなら-1を返す。消費せず、追加のcache領域を持たない。
  reset/end後も別のpeek cacheに古いbyteを残さない。
- CDCのcopy writeはTX timeoutまで空きを待ち、flushは送信完了を待つ。期限0ならどちらも待たない。
- readString等を選べばArduino Stringの費用を払う。通常のread/readBytesにStringを介在させない。

Streamの既定timeoutは1000 ms。setTimeoutの値はFunction objectの状態として保持し、
end→beginで別の値へ自動変更しない。VendorはStreamではないが、非消費のpeekは同じbyte転送層で使える。

### 採用のための最小比較

Core3.3.12/S3、同じ小さなCDC echoのprobeで比較した。v2との性能比較ではない。

| 案 | Flash増分 | 静的RAM増分 | 判断 |
| --- | --- | --- | --- |
| headerだけ含む未使用adapter | 0 | 0 | 未使用時はリンクされない |
| 使用時だけStream adapterを追加 | 524 byte | 24 byte | objectは20 byte。利用側に別objectが必要 |
| 共通bulk baseをStream化 | 412 byte | 8 byte | Vendor/MIDIまでStream扱いになるため不採用 |
| 共通転送層からPrintを分離しCDCだけStream化 | 636 byte | 8 byte | 採用。CDCを直接Streamへ渡せる |

同toolchainでのsizeofはCDC 224→232、Vendor 168→168、MIDI 208→200、共通bulk 152→144 byte。
CDCのStream stateが8 byte増える一方、MIDIから不要なPrint stateを除く。
数値はこのprobeの記録で、全アプリ/他SoCのFlash増分を保証しない。

## CDC送信の期限とflush

`setTxTimeoutMs(timeout)`でcopy送信の待ち期限を指定する。既定1000 ms、0は即時partial。
`txTimeoutMs()`で現在値を読める。稼働中も変更でき、各write/flushは呼出し開始時の値を使う。
読出し用のStream::setTimeoutとは独立し、どちらもend→beginで保持する。

空きがあればwriteはコピーしてすぐ戻る。要求長がringを超える場合も、通常taskでは期限内で
残りを送り、受理した総数を返す。printも同じ経路を使い、単純なconsole.printの操作は維持する。
切断/reset/停止や期限切れではpartial/0を返し得る。無欠落を保証するものではない。
DTRはこれまでどおりwriteの必須条件ではなく、connected=falseでもHost構成済みなら送れる。

flushはpendingのcopy送出を開始し、data/DMAと必要なZLPまで期限付きで待つ。
Hostアプリの読出し完了を意味しない。期限切れでもpending dataを捨てず、後でHostが再開すれば続行する。
voidなので期限切れを戻り値では通知しない。`autoFlush=false`でもflushは送出を始める。
copy writeが空き待ちへ入る場合も送出を進めるので、大きなwriteが満杯のまま待ち続けない。
期限0のwriteだけならautoFlush=falseの蓄積動作を維持する。

USB workerのcallbackからのwriteは即時partial、flushは送出開始のみ。
workerが自分の完了を待って停止することを防ぐ。ISRではwriteは0、flushは何もしない。
借用writeDirect、Vendor/MIDI、共通bulkのwrite/flushにはこの待ちを加えない。
CDC objectだけにtimeoutとreset/停止の世代を持ち、新しいring/待ち専用task/同期objectは確保しない。

## Arduino Printからの空き容量照会

CDC/Vendorは`Print&`として渡せる。`availableForWrite()`は現在copy writeが受理できる
byte数を返す。Arduinoの戻り値型がintなのでINT_MAXで飽和し、size_tの`writeAvailable() const`は
同じ空きをsize_tで返す。借用TX中、停止中、TX ringなし、対象INのHALT中は0となる。
照会後の予約やwire完了を保証するものではなく、writeの受理数も確認する。

```cpp
Print &output = console;
if (output.availableForWrite() > 0) output.write(uint8_t('!'));
```

修正前は継承したPrintの既定実装が常に0を返し、実際の空きが64/128 byteある場合も
標準APIだけを使う利用側が送信を始められなかった。CDC/VendorのPrint側で既存のvirtual関数をoverrideし、
追加のbuffer・task・状態を持たずに実効空きを返す。独自名への書換えを利用側へ要求しない。
MIDIはpacket APIを使う。後続のStream整理ではPrint継承自体を撤去したため、Printへupcastできない。

## コピー送信の終端

`streamTermination`は`EspUsbStreamTermination`で指定する。

- `FunctionDefault`: VendorはContinue、CDCはEndWithShortPacket。
- `Continue`: ringが空になっても追加ZLPを送らない。
- `EndWithShortPacket`: コピー送出がMPS倍数で終わり、ringが空ならZLPまで送る。

CDCの既定値は、Hostの大きなreadへ少量のserial dataを渡せる動作を保つためである。
Vendorは従来のv3のContinueを維持し、必要なら同じ設定で明示できる。
ZLPが不要な連続streamではContinueを選べる。ZLPは一回のwriteをメッセージ境界にする契約ではなく、
複数writeが蓄積されていればまとめて送出される。CDCのflushはdataと必要な終端ZLPの完了まで期限付きで待つ。
Vendor/共通bulkのflushは従来どおり送出開始のみ。
ZLP送出中に次の送信を割り込ませず、コピー完了通知へ0 byte通知を追加しない。
借用TXの`EspUsbTransferTermination`は転送単位の明示指定のままで、stream設定とは別である。

## 借用転送と未使用資源

CDCもwriteDirect/readDirect、方向別完了callback、cancelDirectTx/cancelDirectRxを使える。
投稿結果と完了のid、成功/取消し/reset/end、DMA停止後の一度だけの返却はVendorと共通。
RXをBorrowed、rxCapacity/txCapacity/transferBytesを0にすれば、copy用ring/packetを確保しない。
CDC固有にはline coding用の専有DMA/cache領域64 byteをbegin時に確保し、endで解放する。
未使用CDCの固定instance配列、FIFO、endpoint bufferはリンクしない。

## 変更の影響

- `CFG_TUD_CDC`や`CFG_TUD_CDC_*_BUFSIZE`で容量/個数を指定する処理は移行対象。
  TinyUSB CDCの再有効化は拒否する。容量はFunctionのconfigureへ移す。
- `onLineCoding/onLineState/onRx/setName`はesp_err_tを返す。戻り値を無視していた呼出しは形を維持できるが、
  稼働中の変更はエラーになるため開始前へ移す。
- lineCoding/lineStateの戻り値は参照から値へ変更。取得済み値が後から変わることを前提にせず、再取得する。
- `Device::maxCdcPorts()`は廃止。個数だけの固定上限では他Functionとのendpoint競合を表せない。
  構成全体をbeginで検証し、失敗理由を確認する。registryは必要数で確保し、descriptor/endpointの制約を検証する。
- SEND_BREAKはonBreakで期間を通知する。0/0xffff等の意味はUSB CDCの要求値のまま渡す。

利点はruntime設定、未使用資源の削減、同じ所有権・エラー契約、callback登録時期の統一。
不利益はbegin時の確保失敗、停止してからの再設定、借用buffer寿命管理、snapshot取得への移行である。

## 実験と残り

以下は段階ごとの実測記録。記載時点の未完了事項を現在のAPI制約と混同しない。
現行契約は上記と[移行ガイド](V3_MIGRATION.ja.md)、直近の検証は[validation](../validation/README.ja.md)を参照。

S3–S3の新規`validation/s3/cdc_stream`で、2 CDCの容量を小/大/非power-of-twoへ同一binaryで変更し、
両portの1/63/64/65/127/128/129 byte、line codingの独立性、借用RX/TXを確認した。
S3の非制御IN endpointは4本なので、2 CDCとCDC+Vendor+HIDは別の構成で試験する。
後者ではCDC/Vendor通信に加えてHID keyの押下・解放まで確認する。初回は1 passed、45.53秒。

これはWindows/Linux/macOSのCDC相互接続や、通知endpointの全機能、全control要求の合格ではない。
HID自体の独自化、動的registry/descriptors、残クラスとcore/DCDの全置換、P4/S2/S31の実機試験は継続する。

最終の標準実行は`cdc-vendor-all.xml`、6 passed、340.68秒。CDCを含む46取得点の確保失敗/回復と、
既存v3の取消し・reset/end・大きな転送・Vendorの明示コピー終端も含む。
Device only / Vendor copy / CDC copyのnmで未使用Function固有処理・取消し・TinyUSB CDC/Vendorが残らないことを確認した。
RXを完全に無効化する送信専用modeの明示と、固定registry/descriptor領域の撤去は次の作業である。

## RXを使わない送信専用構成

`rxMode=EspUsbBulkRxMode::Disabled`、`rxCapacity=0`を指定する。RX callbackは不要で、
RX ring/packetを確保せず、OUTをarmしない。USB descriptorの必須OUT endpointは残る。
`available/read`は0、`maxDirectReceive()`は0、`readDirect()`はNotSupportedを返す。
TXは従来通りcopyまたはborrowedを選べる。借用TXはTX完了callbackが必要。
これはBorrowed RXの未投稿状態とは異なり、受信操作自体を使わない明示設定である。


## Hostによるinterface/endpointの初期化

bulk dataのSET_INTERFACE、HALT/CLEAR_FEATUREは[Vendorと共通の回復契約](V3_VENDOR_API.ja.md#標準要求による転送の再初期化)を使う。
data interfaceの再選択は借用TX/RXをCancelledで返し、copy queue/ringを破棄する。
control interfaceの再選択では通知endpointだけを初期化し、data転送、DTR、line codingを維持する。
HALTされたINへのcopy write/writeAvailableは0となる。APIの形は維持し、Host要求時の動作を明確にする。
S3–S3のbulk_standardでVendor+CDCと2 Vendorを別構成で検証した。OS互換性や通知送信の実装完了は含まない。


## SET_LINE_CODINGの受理条件

7 byteの要求長に加え、DATA段階で受信実長が7 byteであることと設定値を検査する。
検査に失敗すればSTALLし、lineCodingとonLineCodingの通知を更新しない。
設定値の反映と通知は成功したstatusの後に行う。

2026-10-27のS3 Host SDKでは、SETUPのwLength=7と送信量0～6 byteを組み合わせると
usb_host_transfer_submit_controlがESP_ERR_INVALID_ARGを返し、wireへ送信しなかった。
この条件の物理short OUTの検証は未完了。SETUP自体の要求長0～6 byteの拒否とは区別する。
Host側の検査は[ESP-IDFのUSBH実装](https://github.com/espressif/esp-idf/blob/v5.5.1/components/usb/usbh.c#L232-L248)にもある。
実機の追加試験では、要求長0～6 byteと、baud=0、stopBits=3、parity=5、dataBits=4/9を拒否し、
各要求の後にGET_LINE_CODINGで直前の設定が維持されることを確認する。
正常なSET_LINE_CODING一回だけがcallbackを発生させることも確認する。


## Print照会の実機確認

修正前はCDCの空き64/128に対してPrint照会0で失敗を再現（1 failed、51.79秒）。
修正後のCDC/Vendor/MIDIは3 passed、337.64秒。20回/10構成で標準Printと実効空きが一致し、
同じPrint経由の送信もHostでpayloadを確認した。6 serial全てSTOPPED、panicなし。
S2/P4 compile、各targetの依存・symbol確認も成功。
詳細は[validation記録](../validation/README.ja.md#printの空き容量の修正前後)。


## native Streamの関連検証

CDC/Vendor/MIDI、確保失敗、Function登録の**5 passed、0 skipped/error/failure/unfinished、465.84秒**。
4容量構成（1 byte ringを含む）でStream経由の空/timeout、繰返しpeek、readBytes、wire echoを確認。
Vendorの非消費peekも1 byte ringで確認した。Print経由の24照会/12構成も一致。
774取得点で失敗/回復し、heapは346704→346704 byte。10 serial全てSTOPPED、panicなし。
MIDIはPrint/Streamでないことをcompileで検証し、既存のpacket転送も通過した。
S2/P4 compileと新CdcConsole例のS3 compileも成功。実機はS3–S3のみ。
原本`/tmp/pytest-embedded/2026-10-28_12-23-52-104927/`、結果と集計は
`validation/.pytest-results/cdc-native-stream-related*`。
最小比較のsource/nm/layoutは同ディレクトリの`stream-layout-*`に保存した。


続く製品`56a2c9a`固定の全34件も成功（2510.86秒、0 skipped/error/failure/unfinished）。
68 serial全てSTOPPED、panicなし、774取得点の回復とheap前後同量を確認した。
詳細は[一括結果](../validation/README.ja.md#native-stream変更後の全34件)。


## CH32RV調整後のTX待ちの実測（2026-10-30）

新規cdc_txの3件、既存cdc_stream/bulk_standard/audio_cdcの各1件、P4 loopback6件の
計12件を標準combo allの一回で成功（374.48秒、skip/error/failure/unfinished 0）。
18 serial logs全て正常停止、panic/heap破損なし。P4は両controllerのFS linkである。

新規S3 probeは64-byte TX ring・64-byte stagingでautoFlushの有無を変え、
DTR=1/0の各4097-byte printを照合した。Hostをbus suspendして実際に送信を止め、
timeout0と60 ms、ringは空だがDMAだけ未完了のflush、resume後の送出を確認した。
worker callbackは64 byteの即時partial、待ち途中のDevice.endは1000 msの期限前に復帰した。
TX容量0なら1000 ms設定でも即座に0を返す。新規caseのHost受信16900 byteは全て内容一致。
ISR経路を実機で発火した試験ではない。

初回はring空きとDMA stagingを同じ容量とみなしたprobeの期待で失敗した。
copy TXはDMAへ移した時点でringの空きが戻るため、停止HostへはDMA＋ringの計128 byteを受理し得る。
この区別へ期待を直して2件成功後、TX無効化を追加し、最終12件も成功した。
記録はvalidation/.pytest-results/cdc-tx-{initial,after,related}*、cdc-tx-audit.json。

S2 CdcConsoleもcompile成功。29 source依存/未使用class state除去を監査した。
今回のS2 buildではCDC objectは240 byteで、Stream化時の232 byteから8 byte増える。
増加はCDCだけのtimeout/停止世代で、新しいheap領域や待ちtaskは追加しない。
S2実機の結果ではない。記録cdc-tx-s2*。
標準allの収集はS3 50＋P4通常28＋PSRAM2＝80件。全80件を一括実行した結果ではない。
