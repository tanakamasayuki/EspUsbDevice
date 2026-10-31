# Vendor API（v3）

`EspUsbDeviceVendor` は、独自形式のデータをbulk転送でやり取りするためのFunctionです。
Host側は専用アプリ（libusb、pyusb、WinUSBなど）でinterfaceを開きます。serial terminalからは開けません。
送受信の仕組み（ring、DMA、借用転送、取消し）はCDCと共通です。CDC固有の動作は[CDC API](V3_CDC_API.ja.md)を参照してください。

用語：

- **MPS**（Max Packet Size）：1 packetの最大長。bulkではFSが64 byte、HSが512 byte。
- **ring**：送受信データを一時的にためる環状buffer。DMAは直接使わない。
- **借用転送**：アプリのbufferをコピーせず、そのままDMAに渡す送受信（`writeDirect`/`readDirect`）。
- **ZLP**（Zero Length Packet）：長さ0のpacket。MPSの倍数で終わる転送の終わりをHostへ知らせる。

## 基本の使い方

```cpp
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceVendor vendor(device);

void setup() {
  EspUsbVendorConfig config;      // EspUsbBulkStreamConfig の別名
  config.rxCapacity = 2048;
  config.txCapacity = 4096;
  if (vendor.configure(config) != ESP_OK) return;  // 停止中だけ成功する
  if (!device.begin()) {
    // 失敗理由は device.lastError() / device.lastErrorName() で分かる
    return;
  }
}

void loop() {
  uint8_t buffer[64];
  const size_t n = vendor.read(buffer, sizeof(buffer)); // 届いている分だけ返す
  if (n) vendor.write(buffer, n);  // 受け付けたbyte数を返す（全部とは限らない）
}
```

### 主な設定

`EspUsbVendorConfig`（= `EspUsbBulkStreamConfig`）の項目です。

| 項目 | 既定値 | 意味 | FS/HSでの違い |
| --- | --- | --- | --- |
| `rxCapacity` | 512 | 受信ringの容量（byte）。`rxMode=Stream`では1以上、それ以外では0にする | なし |
| `txCapacity` | 512 | 送信ringの容量（byte）。0なら`write()`は使えず、借用送信だけになる | なし |
| `rxBuffer` / `txBuffer` | `nullptr` | ringに使う領域を自分で渡すときに指定。`nullptr`なら`begin()`で自動確保 | なし |
| `transferBytes` | 512 | コピー送信で1回にDMAへ渡す最大byte数（1〜65535）。`txCapacity`が1以上なら0は不可 | MPS未満でも受け付ける（FS・HSとも） |
| `autoFlush` | `true` | `true`なら`write()`のたびに送信を始める。`false`なら`flush()`まで溜める | なし |
| `streamTermination` | `FunctionDefault` | コピー送信の終わりにZLPを付けるか。VendorのFunctionDefaultは`Continue`（付けない） | ZLPが付く長さがMPSで変わる |
| `rxMode` | `Stream` | 受信の方式。`Stream`/`Callback`/`Borrowed`/`Disabled`（[受信の方式](#受信の方式)） | なし |

コンストラクタの第2引数 `endpointSize`（既定64）は、FS接続のときのbulk MPSです。HS接続では常に512になります。

## 設定と開始・停止

- `configure()`は停止中だけ成功します。稼働中は`ESP_ERR_INVALID_STATE`、値の不正は`ESP_ERR_INVALID_ARG`を返し、どちらも元の設定を保ちます。
- `begin()`の時点でringとDMA用bufferを確保します。失敗すると確保したものを戻し、USBは開始しません。
- 設定を変えるときは `device.end()` → `configure()` → `device.begin()` の順にします。再ビルドや`build_opt.h`は要りません。
- `device.end()`が`ESP_OK`を返したら、確保した資源は解放済みです。USBのcallbackの中から呼ぶと`ESP_ERR_INVALID_STATE`で拒否されます。
- callback（`onRx`、`onRxData`、`onTxComplete`、`onDirectTxComplete`、`onDirectRxComplete`、`onControlRequest`）の登録も停止中だけです。稼働中は`ESP_ERR_INVALID_STATE`を返すので、無視されたことが分かります。
- `configuration()`は要求した設定を、`readCapacity()`/`writeCapacity()`は実際に確保した容量を返します。後者は開始前と停止後は0です。

`configure()`が`ESP_ERR_INVALID_ARG`を返す主な条件：

- `txCapacity`が1以上なのに`transferBytes`が0
- `rxMode=Stream`で`rxCapacity`が0、または`Stream`以外で`rxCapacity`が0でない
- `rxBuffer`/`txBuffer`を渡したのに対応する容量が0、またはRXとTXの領域が重なっている

`begin()`の失敗理由（`device.lastError()`で読む）：

| 値 | 原因 |
| --- | --- |
| `ESP_ERR_INVALID_ARG` | `rxMode=Callback`で`onRxData`が未登録、または`rxMode=Borrowed`で`onDirectRxComplete`が未登録 |
| `ESP_ERR_NO_MEM` | ringやDMA用bufferを確保できなかった |
| `ESP_ERR_INVALID_STATE` | 既に開始している |

指定した大きさを黙って縮めることはしません。

## コピー方式の送受信

### write と flush

- `write()`はデータをringへコピーし、受け付けたbyte数を返します。ringが足りないと一部だけ受け付けます。戻った時点で元のbufferは再利用できます。
- 戻り値は「ringに入った量」です。Hostへ届いたことは意味しません。
- `autoFlush=true`（既定）なら、1 byteでもすぐに送信を始めます。`false`なら`flush()`を呼ぶまでringに溜めます。
- Vendorの`flush()`は送信を**始める**だけで、完了を待ちません。CDCの`flush()`は完了まで待つ点が違います（[CDCの期限付き送信](V3_CDC_API.ja.md#cdc送信の期限とflush)）。
- `waitWritable(n, timeoutMs)`（既定100 ms）は、空きがn byte以上になるまで待ちます。nが`writeCapacity()`より大きいとすぐ`false`を返します。待ちは1 tickずつのpollingです。

### 空き容量の照会

- `availableForWrite()`は今`write()`で受け付けられるbyte数です。`Print&`として渡しても同じ値が返ります。`int`に収まらない値は`INT_MAX`になります。
- `writeAvailable()`は同じ値を`size_t`で返します。
- 借用送信中、停止中、TX ringなし、IN endpointがHALT中は0です。照会しても領域は予約されないので、`write()`の戻り値も確認してください。

### read

- `read(buffer, size)`は届いている分だけ返し、待ちません。`read()`は1 byte（なければ-1）を返します。
- `peek()`は先頭の1 byteを消費せずに返します（空なら-1）。
- ringが1 packetより小さくても受信データは失われません。入りきらない残りはDMA用bufferに置いたまま、読み終わるまで次の受信を止めます。そのため`available()`が`readCapacity()`を超えることがあります。

### メッセージの区切りとZLP

コピー方式はbyte streamです。複数の`write()`がまとめて送られることがあり、1回の`write()`がHostの1回のreadになるとは限りません。
Host側は長さをprotocolに含めるなどして区切ってください。

`streamTermination`でコピー送信の終わり方を選べます。

| 値 | 動作 |
| --- | --- |
| `FunctionDefault` | Vendorでは`Continue`、CDCでは`EndWithShortPacket` |
| `Continue` | ringが空になってもZLPを送らない |
| `EndWithShortPacket` | 送信がMPSの倍数で終わり、ringが空ならZLPも送る |

「この転送をここで終える」と明示したい場合は、借用送信の`EndWithShortPacket`を使います。

## 受信の方式

`rxMode`で選びます。

| 値 | 受信の受け取り方 | `rxCapacity` |
| --- | --- | --- |
| `Stream` | `available()`/`read()`で読む | 1以上 |
| `Callback` | `onRxData(data, length)`で受け取る。dataはcallbackの間だけ有効なので、残すなら中でコピーする | 0 |
| `Borrowed` | `readDirect()`でアプリのbufferを渡して受け取る（[借用受信](#借用受信readdirect)） | 0 |
| `Disabled` | 受信しない。OUT endpointは記述子に残るが、受信の準備をしない | 0 |

`Disabled`では`available()`/`read()`は0、`maxDirectReceive()`は0、`readDirect()`は`NotSupported`です。
`Borrowed`でbufferを渡していない状態とは違い、受信そのものを使わない設定です。

`onRx(callback)`は受信のたびに`available()`の値を通知します。

## 借用転送（コピーなし）

大きなデータや速度が必要なときは、アプリのbufferをそのままDMAに渡せます。

bufferの条件（送信・受信とも）：

- 先頭アドレスが64 byte境界
- 全領域がDMAで使える内部RAM。PSRAMは使えません（`NotDmaCapable`）
- 完了callbackが返るまで、bufferを変更・解放しない

### 借用送信（writeDirect）

```cpp
// begin前に登録する。callbackはUSB workerで呼ばれるので、待ったりend()を呼んだりしない
vendor.onDirectTxComplete([](const EspUsbTransferResult &result) {
  // result.buffer がアプリへ戻ってきた。result.status と result.actualBytes を確認する
});

// begin後
auto submission = vendor.writeDirect(dmaBuffer, length,
                                     EspUsbTransferTermination::EndWithShortPacket);
if (!submission) {
  // 受理されなかった。submission.error に理由が入る。bufferはアプリのまま
}
```

- 1つのinstanceで同時に進められる借用送信は1つです。コピー送信のデータが残っている間も`Busy`になります。
- `Continue`はZLPを付けません。`EndWithShortPacket`は長さがMPSの倍数ならZLPまで送ってから完了を返します。
- 途中で短いpacketやZLPは入りません。転送全体に対して完了は1回です。
- 長さの上限は`maxDirectTransfer()`で分かります。32 bitのESPでは2147483647 byteです。この値は「確保できる量」ではないので、bufferの大きさはアプリの予算で決めて上限と比べてください。
- 長さに16 bitの制限はありません。controllerの1回のDMA設定に収まらない長さは、ライブラリが内部で分けます。

### 借用受信（readDirect）

```cpp
EspUsbVendorConfig config;
config.rxMode = EspUsbVendorRxMode::Borrowed;
config.rxCapacity = 0;
config.txCapacity = 0;     // コピー送信も使わないなら0
config.transferBytes = 0;  // txCapacityが0なら0でよい
vendor.configure(config);
vendor.onDirectRxComplete([](const EspUsbTransferResult &done) {
  // done.actualBytes 分だけが受信データ。bufferはアプリへ戻った
});
device.begin();

auto submission = vendor.readDirect(dmaBuffer, capacity);
```

- `capacity`は64の倍数で、かつ**接続中のMPSの倍数**が必要です。FSなら64の倍数、HSなら512の倍数です。合わないと`BadArgument`です。
- bufferがいっぱいになるか、短いpacket/ZLPを受けると完了します。
- 次の受信は、改めて`readDirect()`を呼んで投稿します。投稿していない間はOUTを受け付けず、Hostを待たせます。
- callbackの中から、準備済みの次のbufferを投稿できます。
- 上限は`maxDirectReceive()`で分かります（`maxDirectTransfer()`の値を、MPSと64の大きいほうの倍数へ切り下げた値）。

### 投稿の結果（EspUsbTransferSubmission）

`writeDirect()`/`readDirect()`は`EspUsbTransferSubmission`を返します。`if (submission)`で受理を確認し、拒否されたら`submission.error`に理由が入ります。
拒否されたときは完了callbackは来ず、bufferはアプリのままです。

| `error` | 意味 |
| --- | --- |
| `None` | 受理された。`id`は0でない |
| `NotSupported` | 今の`rxMode`ではその操作を使えない |
| `NotMounted` | USBが未接続、またはendpointがまだ開いていない |
| `BadArgument` | bufferや長さが不正、完了callbackが未登録、受信容量がMPS/64の倍数でない |
| `NotAligned` | 先頭アドレスが64 byte境界でない |
| `NotDmaCapable` | DMAで使えないメモリ（PSRAMなど） |
| `Busy` | 同じ方向で転送が進行中 |
| `TransferFailed` | controllerが受け付けなかった |

### 完了の結果（EspUsbTransferResult）

完了callbackはUSB workerで1回だけ呼ばれます。`buffer`、`requestedBytes`、`actualBytes`、`status`、`actualBytesKnown`、`id`が入ります。

| `status` | 意味 |
| --- | --- |
| `Success` | 完了。`actualBytes`が有効。Hostのアプリが処理したことまでは意味しない |
| `BusReset` | bus resetなどで転送が破棄された。どこまで送れたかは不明（`actualBytesKnown=false`） |
| `Stopped` | `end()`でDMAを止めてから返した。どこまで送れたかは不明 |
| `TransferError` | 下位の転送エラー。分かっている範囲だけ`actualBytes`が有効 |
| `Cancelled` | 取消しによって返した。どこまで送れたかは不明 |

callbackの中でしてよいこと：次の準備済みbufferの投稿、他のtaskへの通知。
してはいけないこと：待つ処理、`end()`、設定変更、Functionの破棄。

### 取消し

- `cancelDirectTx(id)`/`cancelDirectRx(id)`で借用転送の取消しを頼めます。`id`を省略（0）すると現在の転送が対象です。
- `ESP_OK`は「取消しを受け付けた」という意味で、bufferが戻ったわけではありません。bufferは完了callback（`Cancelled`など）で戻ります。
- 対象がない、または`id`が違うと`ESP_ERR_INVALID_STATE`です。古い`id`で新しい転送を取り消すことはありません。controllerが取消しに対応していなければ`ESP_ERR_NOT_SUPPORTED`です。
- 同じ転送への2回目の要求は`ESP_OK`です。resetや`end()`と重なると、`BusReset`/`Stopped`で返ることがあります。
- `id`は`begin`/`end`をまたいでも増え続けるので、前回の転送と取り違えません。
- コピー方式の`write()`/`read()`は取消しの対象外です。

## 標準要求による転送の再初期化

Hostから次の標準要求が来ると、VendorとCDCのbulk転送は対象の転送を止めて初期化します。
借用中のbufferは、DMAを止めて古い完了を処理し終えてから1回だけ返します。返るまで、同じ方向の新しい投稿は拒否されます。
要求へのACKとcallbackの時点は同じとは限りません。

| Host要求 | 対象 | 借用中の転送の結果 | data toggle（DATA0/1） |
| --- | --- | --- | --- |
| SET_INTERFACE（alt 0） | 指定したdata interfaceの送受信 | `Cancelled` | 両方向DATA0に戻す |
| SET_FEATURE ENDPOINT_HALT | 指定したendpointだけ | `TransferError` | HALTのまま |
| CLEAR_FEATURE ENDPOINT_HALT | 指定したendpointだけ | `Cancelled` | DATA0に戻す |

- 既に`TransferError`で返却待ちの転送は、続くCLEAR_FEATUREで`Cancelled`に変わりません。resetや`end()`が先なら`BusReset`/`Stopped`のままです。
- 他のFunctionや、指定されなかった方向の転送には影響しません。
- コピー方式でも、対象方向の送信ringと受信ringは捨てられます。`write()`で受け付け済みでも未送信のデータは失われることがあります。確実に届ける必要があるなら、アプリ側で確認と再送をしてください。
- INがHALT中、または初期化の完了待ちの間は、`write()`と`writeAvailable()`は0です。OUTは回復後に受信を再開します。
- この処理はライブラリが内部で行います。監視したいときは`device.onAnyControlRequest()`を使ってください。Vendorのcontrol callbackで置き換えることはできません。
- CDCでcontrol interfaceにSET_INTERFACEが来た場合は、通知endpointだけを初期化します。data転送、DTR、line codingは保たれます。

## Vendor control要求

`onControlRequest(callback)`でVendor/Class要求を受け取ります。callbackは各段階（SETUP、DATA、ACK）で呼ばれます。
data段階の送受信は`sendControlResponse(request, data, length)`で始めます（INなら送るデータ、OUTなら受信先）。

`EspUsbDeviceVendorControlRequest::actualBytes`は、data段階で実際に転送したbyte数です。

- SETUPでは0、DATAとACKではその要求で転送した合計です。IN/OUTの両方向で使えます。
- SETUP packet、status packet、末尾のZLPは数えません。`wLength`はHostが要求した長さのままです。
- 応答がHostの要求長で切り詰められた場合や、短い応答の場合も、この値で区別できます。

```cpp
vendor.onControlRequest([&](const EspUsbDeviceVendorControlRequest &r) {
  if (r.bmRequestType != 0x40 || r.bRequest != 0x61 ||
      !r.wLength || r.wLength > sizeof(commandBuffer)) return false;
  if (r.stage == ESP_USB_DEVICE_CONTROL_STAGE_SETUP)
    return vendor.sendControlResponse(r, commandBuffer, r.wLength); // OUTの受信先を渡す
  if (r.stage == ESP_USB_DEVICE_CONTROL_STAGE_DATA)
    return r.actualBytes == r.wLength && validateCommand(commandBuffer, r.actualBytes);
  if (r.stage == ESP_USB_DEVICE_CONTROL_STAGE_ACK)
    applyCommand(commandBuffer, r.actualBytes);
  return true;
});
```

- 受け付けるかの検査はDATAで、成功した要求の反映はACKで行います。SETUP/DATAで`false`を返すと要求を拒否（STALL）します。
- ACKは成功後の通知です。ここで`false`を返しても、完了した要求は取り消せません。
- `rawRequest`は応答用の内部参照です。保存して後から応答する用途には使えません。

## FSとHSの違い

| 項目 | FS（12 Mbps） | HS（480 Mbps） |
| --- | --- | --- |
| bulk MPS | 64（コンストラクタの`endpointSize`で変えられる） | 512 |
| `transferBytes` | 1〜65535。MPS未満でもよい | 同左 |
| `readDirect()`の容量 | 64の倍数 | 512の倍数 |
| `maxDirectReceive()` | `maxDirectTransfer()`を64の倍数へ切り下げた値 | 512の倍数へ切り下げた値 |
| ZLPが付く長さ（`EndWithShortPacket`） | 64の倍数 | 512の倍数 |

- HSを使えるcontroller（P4のHS controllerなど）では、受信用のDMA bufferを512 byteで確保します。FSで接続した場合もこの大きさのままです。
- 非control INのendpointは、S3が4本まで、P4のHS controllerが7本までです。VendorはINを1本使います。

## PSRAMとバッファ

| 領域 | 置ける場所 |
| --- | --- |
| `rxBuffer`/`txBuffer`（ring） | 内部RAMでもPSRAMでもよい。境界の条件もない。PSRAMのringはFS loopbackとHS直結の両方で確認済み |
| 自動確保のring | 通常の`malloc`で確保する（置き場所はsdkconfigのmalloc設定による） |
| DMA用bufferとコピー送信のstaging | ライブラリが内部RAM（DMA可能、64 byte境界）に確保する |
| 借用転送のbuffer | DMA可能な内部RAMが必要。PSRAMは`NotDmaCapable`で拒否 |

`rxBuffer`/`txBuffer`はライブラリが解放しません。`begin()`から`end()`まで保持してください。大きさの管理もアプリの責任です。

## 2.xと初期のv3からの移行

- `build_opt.h`でのVendor容量や方式の指定は不要です。`CFG_TUD_VENDOR`は使いません。容量は`configure()`で指定します。
- 起動前に容量を知りたいときは`configuration()`から要求値を読みます。
- 旧`onTxComplete`はコピー送信の通知です。借用bufferの返却は、statusが付いた`onDirectTxComplete`を使います。
- `device.end()`は`esp_err_t`を返します。bufferやFunctionを破棄する前に`ESP_OK`を確かめてください。
- callbackの登録は開始前に移してください。稼働中の登録はエラーになります。
- 旧`flush()`の待ちや、暗黙のZLPに頼ったHost側のreadは、終端の条件を見直してください。
- `bool ok = vendor.writeDirect(...)`は`auto submission = vendor.writeDirect(...)`へ、`lastDirectError`は`submission.error`へ、`onDirectComplete`は`onDirectTxComplete`へ移します。`if (!vendor.writeDirect(...))`の形はそのまま使えます。
- 65535や65472を上限と見なして大きなbufferを確保していたコードは、必要な容量を明示する形に変えてください。
- `EspUsbDeviceVendorControlRequest`にfieldを追加したので、位置指定のaggregate初期化は見直してください。内部の`usbd_control_received_bytes()`を使っていた場合は`r.actualBytes`へ移せます。

## 設計判断の理由

- **instance単位の設定**：容量をbuild設定ではなく`configure()`で持つことで、1つのbinaryで構成を変えられ、使わない資源を確保しません。代わりに、`begin()`での確保失敗と、停止してからの再設定が必要になります。
- **投稿結果を戻り値で返す**：以前はinstanceの`lastDirectError`に残していましたが、続くTX/RXの呼出しで上書きされるため、呼出しごとの`EspUsbTransferSubmission`にしました。
- **借用受信を投稿式に**：内部のpacket bufferが要らず、アプリがbufferの数を決められます。代わりに、明示的な投稿とbufferの寿命・境界の管理が必要です。
- **受信bufferをDMA専用にする条件**：容量をMPSと64の倍数にし、領域全体をDMAに渡すことで、P4などでcacheのinvalidateが周りの変数を壊す状況を避けます。
- **取消しは要求と返却を分ける**：DMAを止め、USB workerのqueueにfenceを置いて古い完了eventを処理してから返します。queueが満杯なら次のworker周回で再試行し、取消しごとのheap確保はしません。DWC2の実装はHostへSTALLせず、DATA PIDも変えずにDMAを止めます。
- **Functionのcopy/moveを禁止**：登録済みのFunctionをコピーすると所有権が壊れるためです。
- **`waitWritable()`はpolling**：待ち用の同期資源を常に確保しないためです。遅延とCPU負荷を測ったうえで、通知式にするかを後で判断します。
- **背圧の扱い**：echoの量がDeviceのRX+TXの保存量を超える場合、Hostが同期OUTを終えてからINを読むと、お互いに待ち続けます。Hostが並行して受信するか、アプリが送受信を分けてください。容量を無条件に増やす約束はしません。
- **標準要求の回復処理**：コピー専用の構成でもabort/fenceのコードがリンクされます。公開の`cancelDirectTx`/`cancelDirectRx`は、使わなければリンクされません。

## 実測の記録

記載時点の結果です。いまの仕様は上の節を見てください。

### HS直結のVendor（2026-10-31）

`validation/peer/vendor_link`。旧P4とP4XをHSで直結し、両方向のcomboで成功しました。各試験を3回繰り返しています。

- Hostから見てspeed=HS、bulk IN/OUTのMPSは512。
- コピー方式のecho：1/511/512/513/4095/4096/4097 byte。
- 借用受信→借用送信：1/511/512/513/4097/65535/65536/65537 byte。完了は1回で、status、実長、buffer、idが一致し、DMA領域の外のguardも壊れていない。受信容量はMPSの倍数へ切り上げて渡す。
- `cancelDirectRx`で完了が1回だけ（`Cancelled`、idが一致）。
- 借用受信を持ったまま`end()`すると`Stopped`で1回だけ返る。

この試験で、P4X Deviceの65535 byteの借用受信が9216 byte以降で書き込まれない不具合を見つけました（完了は成功・65535 byteと報告していた）。
OUT DMAの開始時にbufferのcacheを書き戻していなかったため、DMAの途中で追い出されたcache行が受信データを上書きしていました。
09470d6でOUT開始時にもcache cleanするよう直し、P4/P4 PSRAM/P4X-PC/HS直結の全32件で回帰を確認しました。S3はcache同期のない構成なので影響しません。
詳細は[P4/P4Xと試験環境](V3_P4_VARIANTS_AND_BENCH.ja.md#vendor列挙コピー借用転送2026-10-31)。

### PSRAM上のring（2026-10-30〜31）

- FS loopback（`validation/p4/psram`）：Vendor RX/TX ring（各8 KiB）をPSRAMに置き、160往復・各方向153664 byteを照合（53bcfeb）。
- HS直結（`validation/peer/psram_link`、42d844d）：同じくringをPSRAMに置き、1〜32768 byteの10種を4回echo。8 KiBより大きい転送では、ringが満ちても欠けずに待つことを確認。

詳細は[P4/P4Xと試験環境のPSRAM](V3_P4_VARIANTS_AND_BENCH.ja.md#psram)。

### 65535 byteを跨ぐ借用転送

総長とDMAの進捗を32 bitに広げ、内部のcore/controllerも`uint32_t`の長さを扱うようにしました。
1回のhardware設定は、実装されたcounterの幅と`uint16_t`に収まる大きさに分け、途中の設定はMPSの倍数にします。
途中に短いpacketやZLPは入れず、元のpointer・`requestedBytes`・idに対して1回だけ返します。
Control/ISOの16 bitの投稿上限はそのままです。大きな転送のためにqueue/task/bufferは増やしていません。

- S3実機のpacket counterは7 bitで、MPS 64なら1回の設定は8128 byteまで。分割はライブラリが行う。
- DWC2のendpoint recordは16→20 byte。S3でのcontroller領域はEP0のみ128→128、Vendor単体192→192、CDC＋HID 192→256、sparse構成256→256 byte（cache line単位の切り上げで増える構成がある）。
- 修正前：131072 byteの領域を確保できても、65536 byteの投稿が`BadArgument`で拒否された（1 failed、56.84秒）。
- 修正後：transfer_limits/Vendor/Audio/Video関連5件が成功（275.46秒）。大きな転送中のreset/endとcore/確保失敗の3件も成功（147.55秒）。774か所の確保失敗からの回復とheapの前後一致を確認。
- 最後のFS packetを残す131008 byteの位置でcancel/reset/endする試験も成功（48.66秒）。最大131072 byte、92件の返却、2種類の終端、short/ZLP、返却pointer/id、canaryと全byteを確認。patternにはoffsetの上位bitも混ぜ、64 KiBでpointerが折り返しても一致しないようにした。
- 製品`f1c9f29`固定の全34件も成功（1631.13秒、0 skipped/error/failure）。[一括結果](../validation/README.ja.md#wide-loan変更後の全34件)。
- 結果は`validation/.pytest-results/wide-loan-*`。当時の実機はS3–S3で、S2/P4はcompileのみ（P4のHSは上のvendor_linkで確認）。

### 取消しとreset/endの競合

USB workerを止めた専用の実機試験で、3つの条件を各3回確認しました。

- 取消しの処理が動く前に実際のbus resetが来る
- 取消しの処理が動く前に`end()`する
- DMAを止めた後、fenceを処理する前に`end()`する

### 背圧を含む連続往復

`vendor_soak`で1000 frame、各方向713000 byteを照合しました。39回のHost再列挙、終了後のheap一致、境界の揃っていない外部ringのguardも確認しています（1 passed、59.76秒）。
HostはINをcontinuous callbackで受けながら同期OUTを送りました。
詳細は[実験記録](V3_EXPERIMENTS.ja.md#2026-10-29-e1e2の追加観測cface78)。

### 小さいringと未使用資源

- MIDIと共通化した転送層で、RX/TX/transferBytesをすべて1 byteにしたVendorの往復を確認。
- S3で容量変更、2のべき乗でない外部ring、callback受信、複数instance、reset/endでの返却、ZLP、100回の開始・終了を確認。
- Deviceだけのbuildでは、Vendorの実装がリンクされないことを確認。
