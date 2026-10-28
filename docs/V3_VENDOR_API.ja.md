# v3 Vendor APIの実装と移行

Vendor/CDCは共通の独自bulk driverを使う。共通EP0/coreとDWC2も本体内部で保守し、TinyUSB build設定は使わない。
実機確認はS3–S3、他SoC/HSは未検証。[開始modeと移行](V3_MIGRATION.ja.md)も参照。
共通設定型とCDC固有の契約は[CDCガイド](V3_CDC_API.ja.md)を参照。

## 設定して開始する

```cpp
EspUsbDevice device;
EspUsbDeviceVendor vendor(device);

void setup() {
  EspUsbVendorConfig config;
  config.rxCapacity = 2048;
  config.txCapacity = 4096;
  config.transferBytes = 256;
  // Check errors in application-specific handling.
  if (vendor.configure(config) != ESP_OK) return;
  if (!device.begin()) return;
}
```

設定はinstance単位。`configure()`は停止中だけ成功し、引数不正や稼働中の拒否では元設定を保持する。
`begin()`で必要なring・DMA packet bufferを確保し、失敗時は戻す。`end()`がESP_OKで戻れば解放済みで、USB callbackからの呼出しはInvalidStateで拒否する。
再設定はend→configure→beginで行い、再ビルドしない。稼働中に再度beginするとInvalidStateになる。
callback設定も停止中に行い、各`on*()`の戻り値を確認する。

| 設定 | 意味 |
| --- | --- |
| rxCapacity / txCapacity | byte stream ringの容量。packet size、transfer sizeとは別 |
| rxBuffer / txBuffer | 任意の外部ring領域。nullなら自動確保。非DMA・非alignment領域も可。指定領域はbeginからendまで保持 |
| transferBytes | copy TXの一回の投稿量の上限。1 byteから指定でき、controller内部のDMA armは必要に応じて分割 |
| autoFlush | trueが標準。短いwriteも送出を進める。falseは明示flushでdrainを開始 |
| streamTermination | FunctionDefaultはVendorでContinue。EndWithShortPacketならcopy送出の末尾も必要に応じZLPで終端 |
| rxMode | Streamはavailable/read、CallbackはonRxData。Callback/BorrowedではrxCapacity=0としRX ringを作らない |

TX容量0ではcopy用ringとTX stagingを作らず、borrowed送信だけを使える。
外部ringはライブラリでfreeしない。領域の大きさと寿命は利用者の責務で、RX/TXの重複領域は拒否する。
`configuration()`は要求設定、`readCapacity()/writeCapacity()`は実際の確保量で開始前・終了後は0。
無効な大きさを黙って縮めない。確保失敗は`device.lastError()`のNoMemoryで識別できる。

copy RX ringもMPS未満を指定できる。入りきらない一packetの残りは既存DMA領域に保持し、
readがringと保留分を読み出す。全部消費するまで次をarmせず、上書きしない。
availableは保留分を含むためreadCapacityを超える場合がある。readCapacityはring自体の確保量である。
MIDIと共通化した転送層で、RX/TX/transfer各1 byteのVendor往復も確認した。

## 普通のwriteとまとめ書き

writeは受け入れたbyteをコピーし、そのbyte数を返す。partialを扱い、復帰後は元bufferを再利用できる。
`autoFlush=true`なら一byteでも送出が進む。`autoFlush=false`では明示flushまでringに保持する。
flushは待ち処理ではなく、溜まったdataの送出開始である。空のflushは以降のwriteへ影響しない。

Arduinoの`Print&`へ渡した場合の`availableForWrite()`も実際のcopy送信の空きを返す。
intへ収まらない値はINT_MAXに飽和する。size_tでの照会は`writeAvailable() const`を使える。
停止・借用TX・HALT等の扱いは[共通Print契約](V3_CDC_API.ja.md#arduino-printからの空き容量照会)を参照。

`waitWritable(n, timeout)`は容量を超えるnをclampせず拒否する。現在は1 tick単位の待ちで、
待機用同期資源を常時確保しない。後段で遅延・CPU消費を測り、通知待ちを採るか判断する。

copy streamはメッセージ境界を保証しない。フレーム長をprotocolで持たせるなどしてHost側で扱う。
TinyUSBによる暗黙のZLP挿入を前提にしたHost readは見直しが必要。
明示したshort/ZLP終端を含む一転送を必要とする場合はborrowed送信で指定する。

## 借用送信と返却

`writeDirect()`はコピーしない。DMA可能な内部RAM、64 byte alignmentを要求する。
pointerだけでなく指定領域を転送中に保持し、callbackで返却されるまで変更しない。
callbackを事前登録しない場合、Busy、不適合buffer、長さ上限超過では受理しない。
拒否には完了通知がなく、所有権は呼出側のままである。

```cpp
// Before begin; callback only records ownership and signals another task.
vendor.onDirectTxComplete([](const EspUsbTransferResult &result) {
  returnBufferToProducer(result.buffer, result.status,
                         result.actualBytes, result.actualBytesKnown);
});
// After begin/configuration; context must be ready before the call.
auto submission = vendor.writeDirect(buffer, length,
    EspUsbTransferTermination::EndWithShortPacket);
```

一instanceにつき未完borrowed TXは一つ。copy TXが残る場合もBusyとなる。
上限は`maxDirectTransfer()`で実効接続から照会する。内部の総長と進捗は32 bitで扱い、呼出し長の16 bit制約を撤去した。
controllerの実装カウンタ幅は能力レジスタから読み、内部でDMA armを分割する。
S3実機はpacket数7 bitで、一armの上限はMPS64で8128 byteである。利用者がこの単位に分割する必要はない。
全体に対して一度だけ返却し、途中にshort/ZLPを挿入しない。転送全体の上限はuint32_tとPTRDIFF_MAXの小さい方。32 bit ESPでは2147483647 byteだが、これは確保可能量ではない。

上限をそのまま確保量に使わず、applicationの予算と実際のbuffer容量で投稿量を決める。
例えば128 KiBを使うならその領域を確保し、上限との比較後に投稿する。大きなbufferの確保成功を
maxDirectTransfer()/maxDirectReceive()が保証するわけではない。RX上限はMPS/64 byteの倍数へ切り下げる。
従来の65535/65472を前提に上限分のbufferを確保していたコードは、明示的な容量へ変更する。

```cpp
const size_t capacity = 128 * 1024; // Application budget, not the API ceiling.
if (capacity <= vendor.maxDirectTransfer()) {
  auto submission = vendor.writeDirect(dmaBuffer, capacity);
  // dmaBuffer must cover capacity bytes and remain owned until completion.
}
```

`Continue`はZLPを追加しない。`EndWithShortPacket`はMPS倍数長なら末尾ZLPまで含めて返却する。
data転送とZLPの間に次の送信が割り込まない。普通のwriteの戻り値とwire完了は区別する。

| 完了status | 返却内容 |
| --- | --- |
| Success | actualBytesが有効。peerアプリが処理したことまでは保証しない |
| BusReset | reset/unconfigure等で旧転送を破棄。送信済みprefixは不明なのでactualBytesKnown=false |
| Stopped | endでDMAを停止してから返却。送信済みprefixは不明 |
| TransferError | 下位転送エラー。判明している範囲だけactualBytesを有効にする |
| Cancelled | 個別取消し後、DMA停止と旧完了eventの処理を待って返却。転送済みprefixは不明 |

返却は内部lockを解放してUSB workerで一度だけ行う。endもworkerに停止を依頼し、
DMA停止→返却→worker終了を待つ。callback内では次の準備済みbufferの送信とsignalを行えるが、
blockやend、構成変更、Function破棄を行わない。endをcallbackから呼ぶとInvalidStateで拒否する。
`cancelDirectTx(submission.id)` / `cancelDirectRx(submission.id)`で、その借用転送の取消しを要求できる。
引数省略（id=0）は現在の借用を対象にする。受理時のsubmission.idは非0、拒否時は0で、
完了result.idにも同じ値を返す。instance内でbegin/endを跨いで更新するので、古いidで新しい転送を取り消さない。

取消しのESP_OKは要求の受理であり、bufferの返却ではない。対応する完了callbackまで領域を保持する。
対象なし・id不一致はESP_ERR_INVALID_STATE、未対応adapterはESP_ERR_NOT_SUPPORTEDとなる。
受理済みの同じ転送への再要求はESP_OK。reset/endと競合した場合はBusReset/Stoppedで返却されることもある。
通常のcopy write/readの取消しはこのAPIの対象外である。

現在のDWC2 adapterはHostのSTALLやDATA PIDを変更せずDMAを停止する。
USB workerのFIFOへfenceを置き、それ以前の古い完了eventを処理してから返却する。
event queue満杯では次のworker反復で再試行し、返却まで同方向の新規借用を拒否する。
取消しごとのheap確保は行わない。callback内から次の転送を投稿できる。

## 変更の影響と次の検証

- build_opt.hのVendor容量・buffered/direct指定は不要。`CFG_TUD_VENDOR`を再有効化しない。
- static容量取得はinstance呼出しへ。起動前にはconfigurationから要求値を読む。
- 旧onTxCompleteはcopy送出通知。借用bufferの返却はstatus付きonDirectTxCompleteへ移す。
- Device::endはesp_err_tを返す。bufferやFunctionを破棄する前にESP_OKを確認する。
- callbackの登録/変更は開始前へ移す。稼働中設定を無言で無視しない。
- 旧flushや暗黙ZLPへの依存はwireの終端条件を見直す。自動送出と明示batchを選べる。

S3で容量変更、非power-of-twoの外部ring、callback RX、複数instance、reset/end返却、
ZLP、100回開始終了を検証する。DeviceだけのbuildではVendor実装がリンクされないことも確認する。
他クラスの固定資源はまだ残るため、ライブラリ全体の未使用費用が解決済みとはしない。
P4 DMA/cache、全資源取得点の故障注入、他クラスへの展開を続ける。
cancelとreset/endの競合は、workerを停止した専用実機試験で3条件各3回を確認した。
取消しjob実行前の実bus reset、job実行前のend、DMA停止後・fence実行前のendが対象である。

## 投稿式borrowed RXと呼出しごとの結果

`rxMode=Borrowed`と`rxCapacity=0`を指定し、開始前に`onDirectRxComplete()`を登録する。
`readDirect(buffer, capacity)`は受信領域を一つ投稿する。満杯またはshort packet/ZLPで返却され、
次の受信は明示的に再投稿する。未投稿ではOUTをarmせずbackpressureをかける。
callbackから別の準備済みbufferを投稿できる。reset/endでも同じ返却経路を一度だけ通る。

容量は実効MPSと64の両方の倍数、開始addressは64 byte alignment、全領域がDMA可能RAMであることを要求する。
領域をDMA専有にすることでP4等のcache invalidateが外側の変数を巻き込む条件を避ける。
S3でcanaryを確認した段階であり、P4 cache実機合格の代用にはしない。
`maxDirectReceive()`で現在の上限を照会し、返却のactualBytesだけを受信dataとして扱う。
Borrowed RXでは内部packet bufferを作らず、copy TXも無効ならtransferBytes=0でよい。

TX/RXの投稿は`EspUsbTransferSubmission`を返す。`if (submission)`で受理を確認し、
`submission.error`にその呼出しの拒否理由が残る。後続のTX/RXによる診断値の上書きを避けるため、
instanceのlastDirectErrorは廃止した。完了通知は別の`EspUsbTransferResult`である。
TX callbackも方向を明示して`onDirectTxComplete()`へ整理した。

```cpp
EspUsbVendorConfig config;
config.rxMode = EspUsbVendorRxMode::Borrowed;
config.rxCapacity = config.txCapacity = 0;
config.transferBytes = 0;
vendor.configure(config);
vendor.onDirectRxComplete([](const EspUsbTransferResult &done) {
  // DMA no longer owns the buffer. Process actual bytes, or recycle on reset.
  returnReceivedBuffer(done);
});
device.begin();
auto submission = vendor.readDirect(buffer, capacity);
if (!submission) handleSubmissionError(submission.error);
```

初期v3 APIからの変更は、bool代入を`auto`へ、lastDirectError参照を戻り値のerrorへ、
onDirectCompleteをonDirectTxCompleteへ移す。`if (!vendor.writeDirect(...))`の形はそのまま使える。
利点は並行操作でも因果関係が明確な診断と、内部RX資源の削減。不利益は明示投稿とbuffer寿命・alignmentの管理である。
登録されたFunctionを誤ってcopy/moveすると所有権が壊れるため、Functionのcopy/moveも禁止した。

## RXを使わない送信専用構成

`rxMode=EspUsbBulkRxMode::Disabled`、`rxCapacity=0`を指定する。RX callbackは不要で、
RX ring/packetを確保せず、OUTをarmしない。USB descriptorの必須OUT endpointは残る。
`available/read`は0、`maxDirectReceive()`は0、`readDirect()`はNotSupportedを返す。
TXは従来通りcopyまたはborrowedを選べる。借用TXはTX完了callbackが必要。
これはBorrowed RXの未投稿状態とは異なり、受信操作自体を使わない明示設定である。


## 標準要求による転送の再初期化

VendorとCDCのbulk transportでは、Hostの要求に応じてDMAを停止し、旧完了eventのfence後に
借用bufferを一度だけ返す。要求のACKとcallbackの実行時点は同じとは限らない。
返却まで同方向への新規投稿を拒否し、callbackから再投稿できる。

| Host要求 | 対象 | 進行中の借用の結果 | DATA PID |
| --- | --- | --- | --- |
| SET_INTERFACE、alt 0 | 指定data interfaceのTX/RX | Cancelled | 両方向DATA0 |
| SET_FEATURE ENDPOINT_HALT | 指定endpointだけ | TransferError | HALTを維持 |
| CLEAR_FEATURE ENDPOINT_HALT | 指定endpointだけ | Cancelled | DATA0 |

既に返却待ちのTransferErrorは、続くCLEAR_FEATUREでCancelledへ上書きしない。
reset/endが競合した場合のBusReset/Stoppedも維持する。
他Functionと、endpoint要求で指定していない方向の転送・PIDは維持する。
CDCのcontrol interfaceのSET_INTERFACEは通知endpointだけを初期化し、data interfaceの借用、
DTR、line codingを維持する。現在のCDCはserial-state通知転送自体を投稿しない。

copy modeも対象方向の送信queue・受信ringを破棄するため、writeで受理済みでも未送信のbyteは失われ得る。
INがHALT中、または初期化のfence待ちならwrite/writeAvailableは0を返す。
OUTは回復後に受信を再開する。配送保証が必要な用途はアプリケーション側で確認・再送を行う。
標準要求の回復処理は内部で扱う。監視にはDeviceのonAnyControlRequestを使い、
Vendorの独自control callbackでこの必須処理を置き換えることを前提にしない。

この回復処理にはcopy専用構成でもabort/fenceのコードが必要となる。
取消しごとのheap確保は増やさず、使わない公開cancelDirectTx/cancelDirectRxは最小buildでリンクされない。
以前の「copy専用ではabortもリンクされない」という測定条件は、この標準要求対応で変更した。


## control callbackの実byte数

`EspUsbDeviceVendorControlRequest::actualBytes`でdata段階の転送実量を読む。
SETUPでは0、DATAとACKではその要求で転送した合計を示す。IN/OUTの両方向が対象で、
SETUP/status packetや末尾ZLPをbyte数へ加えない。wLengthはHostの要求量のまま残る。
INの応答をHost要求長で切り詰めた場合や短い返信も、この値で区別できる。
追加heapやbufferは不要で、既存coreの計数値をcallbackの値へコピーする。

```cpp
vendor.onControlRequest([&](const EspUsbDeviceVendorControlRequest &r) {
  if (r.bmRequestType != 0x40 || r.bRequest != 0x61 ||
      !r.wLength || r.wLength > sizeof(commandBuffer)) return false;
  if (r.stage == ESP_USB_DEVICE_CONTROL_STAGE_SETUP)
    return vendor.sendControlResponse(r, commandBuffer, r.wLength);
  if (r.stage == ESP_USB_DEVICE_CONTROL_STAGE_DATA)
    return r.actualBytes == r.wLength && validateCommand(commandBuffer, r.actualBytes);
  if (r.stage == ESP_USB_DEVICE_CONTROL_STAGE_ACK)
    applyCommand(commandBuffer, r.actualBytes);
  return true;
});
```

受理条件の検査はDATA、成功した要求の適用はACKで行う。SETUP/DATAでfalseを返すと拒否する。
ACKは成功後の通知であり、その戻り値で完了済みの要求を取り消せない。
従来内部の`usbd_control_received_bytes()`を使っていた利用は`r.actualBytes`へ移し、
アプリのためだけに追加した内部headerのincludeを外せる。
callback引数へのfield追加のため通常の登録方法は維持するが、構造体の位置指定aggregate初期化は見直す。
rawRequestは返信用の暫定内部参照で、保存して後から返信するための寿命保証は追加していない。


## 65535 byteを跨ぐ借用転送

総長とDMA進捗を32 bitに広げ、Private core/controllerもuint32_tの投稿長を扱う。
各hardware armは実装されたcounter幅とuint16_t内に収め、途中のarmはMPS倍数にする。
中間のshort/ZLPを作らず、元pointer・requestedBytes・idに対して一度だけ返す。
Control/ISOの従来の16 bit投稿上限は維持する。bulkの大きいloanのためにqueue/task/bufferを増やさない。

DWC2 recordは16→20 byte。ISOのinterval/retryとDMA進捗は同時に使うため重ねない。
S3でのcontroller領域はEP0のみ128→128、Vendor単体192→192、CDC＋HID192→256、
sparse構成256→256 byte。cache lineへの丸めで増分が出る構成がある。

修正前は131072 byteの領域を確保できても65536 byte投稿をBadArgumentで拒否（1 failed、56.84秒）。
修正後のtransfer_limits/Vendor/Audio/Video関連5件は5 passed（275.46秒）。
大きなloanのreset/endとcore/確保失敗の3件も3 passed（147.55秒）、774取得点の回復とheap前後同量。
さらに最後のFS packetを残す131008 byteの位置でcancel/reset/endを行う試験が1 passed（48.66秒）。
最大131072 byte、92件の返却、2種類の終端、short/ZLP、返却pointer/id、canaryと全byteを確認。
patternはoffsetの高位bitも混ぜ、64 KiBでpointerが折り返しても一致させない。
S2/P4はcompile成功、実機の確認はS3–S3のみ。結果はvalidation/.pytest-results/wide-loan-*。
