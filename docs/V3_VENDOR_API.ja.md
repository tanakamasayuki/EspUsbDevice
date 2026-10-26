# v3 Vendor APIの実装と移行

2026-10-26、S3–S3で検証中の実装。Vendorクラス処理は独自driverへ移し、
TinyUSBのVendor driverは無効化した。共通EP0、device state、DWC2は引き続き暫定TinyUSB adapterを使う。
他クラスの置換とP4/S2/S31の実機検証はこれからである。

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
| transferBytes | copy TXの一回のarm量の上限。実効MPS以上が必要。下位のpacket数上限でさらに分割 |
| autoFlush | trueが標準。短いwriteも送出を進める。falseは明示flushでdrainを開始 |
| rxMode | Streamはavailable/read、CallbackはonRxData。CallbackではrxCapacity=0としRX ringを作らない |

TX容量0ではcopy用ringとTX stagingを作らず、borrowed送信だけを使える。
外部ringはライブラリでfreeしない。領域の大きさと寿命は利用者の責務で、RX/TXの重複領域は拒否する。
`configuration()`は要求設定、`readCapacity()/writeCapacity()`は実際の確保量で開始前・終了後は0。
無効な大きさを黙って縮めない。確保失敗は`device.lastError()`のNoMemoryで識別できる。

## 普通のwriteとまとめ書き

writeは受け入れたbyteをコピーし、そのbyte数を返す。partialを扱い、復帰後は元bufferを再利用できる。
`autoFlush=true`なら一byteでも送出が進む。`autoFlush=false`では明示flushまでringに保持する。
flushは待ち処理ではなく、溜まったdataの送出開始である。空のflushは以降のwriteへ影響しない。

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
vendor.onDirectComplete([](const EspUsbTransferResult &result) {
  returnBufferToProducer(result.buffer, result.status,
                         result.actualBytes, result.actualBytesKnown);
});
// After begin/configuration; context must be ready before the call.
bool accepted = vendor.writeDirect(buffer, length,
    EspUsbTransferTermination::EndWithShortPacket);
```

一instanceにつき未完borrowed TXは一つ。copy TXが残る場合もBusyとなる。
上限は`maxDirectTransfer()`で実効接続から照会する。暫定DCDには16bit長と10bit packet数の
両方の制限があり、S3のMPS64では65472 byte。新DCDへ移す際にこの上限を公開定数として固定しない。

`Continue`はZLPを追加しない。`EndWithShortPacket`はMPS倍数長なら末尾ZLPまで含めて返却する。
data転送とZLPの間に次の送信が割り込まない。普通のwriteの戻り値とwire完了は区別する。

| 完了status | 返却内容 |
| --- | --- |
| Success | actualBytesが有効。peerアプリが処理したことまでは保証しない |
| BusReset | reset/unconfigure等で旧転送を破棄。送信済みprefixは不明なのでactualBytesKnown=false |
| Stopped | endでDMAを停止してから返却。送信済みprefixは不明 |
| TransferError | 下位転送エラー。判明している範囲だけactualBytesを有効にする |

返却は内部lockを解放してUSB workerで一度だけ行う。endもworkerに停止を依頼し、
DMA停止→返却→worker終了を待つ。callback内では次の準備済みbufferの送信とsignalを行えるが、
blockやend、構成変更、Function破棄を行わない。endをcallbackから呼ぶとInvalidStateで拒否する。
個別endpointのcancelと投稿式borrowed RXは未実装。暫定TinyUSBには古い完了eventを識別するtokenがなく、
stallだけで安全な取消しを装わず、下位層の世代管理と一緒に実装する。

## 変更の影響と次の検証

- build_opt.hのVendor容量・buffered/direct指定は不要。`CFG_TUD_VENDOR`を再有効化しない。
- static容量取得はinstance呼出しへ。起動前にはconfigurationから要求値を読む。
- 旧onTxCompleteはcopy送出通知。借用bufferの返却はstatus付きonDirectCompleteへ移す。
- Device::endはesp_err_tを返す。bufferやFunctionを破棄する前にESP_OKを確認する。
- callbackの登録/変更は開始前へ移す。稼働中設定を無言で無視しない。
- 旧flushや暗黙ZLPへの依存はwireの終端条件を見直す。自動送出と明示batchを選べる。

S3で容量変更、非power-of-twoの外部ring、callback RX、複数instance、reset/end返却、
ZLP、100回開始終了を検証する。DeviceだけのbuildではVendor実装がリンクされないことも確認する。
他クラスの固定資源はまだ残るため、ライブラリ全体の未使用費用が解決済みとはしない。
P4 DMA/cache、全資源取得点の故障注入、個別cancel、投稿式RX、他クラスへの展開を続ける。
