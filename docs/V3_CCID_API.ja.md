# v3 CCIDのruntime構成

明示的なDevice/Function/config/beginを維持し、CCIDの容量とカード固定設定をFunctionごとに扱う。
TinyUSBのCCID classは使わず、独自coreへ独自driverを登録する。DWC2と内部の暫定型/ABI依存はまだ残る。

```cpp
EspUsbDevice device;
EspUsbDeviceCcid reader(device);
EspUsbCcidConfig config;
config.messageBytes = 320;
config.name = "Card reader";
reader.configure(config);
const uint8_t atr[] = {0x3b, 0x80, 0x01, 0x81}; // T=1
reader.insertCard(atr, sizeof(atr));
reader.onApdu([](const uint8_t *, size_t, uint8_t *reply, size_t capacity) -> size_t {
  if (capacity < 2) return 0;
  reply[0] = 0x6d; reply[1] = 0; // application-specific command handling
  return 2;
});
if (!device.begin()) {
  // device.lastError() distinguishes invalid configuration and allocation failure.
}
```

## 容量と費用

messageBytesは10 byte headerを含む全message容量。既定320、64～65535 byteで、
選択controllerの最大packetより小さい構成はbeginで拒否する。
同じ値をdwMaxCCIDMessageLengthへ出し、callbackの出力容量はmessageBytes−10となる。
大きい容量を設定してもextended APDU、chaining、PIN処理の対応は追加されない。
T=1・short APDUを扱うemulated slotという既存の機能範囲を維持する。

選択したFunctionごとにstate、OUT packet DMA、message組立て領域、返信DMAをbegin時に確保する。
着脱可能なら通知DMAも確保し、固定カードでは省く。返信DMAをcallbackの出力先にも使い、
INと末尾ZLPの完了まで次のOUTを投稿しない。1 slot / 1 busy commandとしてbackpressureをかける。
通常の転送ごとのheap確保やCCID専用taskはない。未使用CCIDのdriverやbufferはリンクしない。
ATRの33 byte配列はprotocol上限として選択したFunction内に残る。

2つのCCIDを別interfaceとして登録できる。実際の上限は構成全体のendpoint資源による。
S3では着脱可能なCCID 2つが非制御IN 4本を使うため、それにVendorを追加できるとは限らない。
各FunctionのmessageBytes、カード状態、callback、送受信bufferは別々に持つ。

## カード固定と通知

removable=trueが既定。insertCard/removeCardの変更を通知endpointで報告する。
通知が未読でも最新状態の変更を保留し、先の通知の完了後に送る。
ATRの交換とPowerOnのsnapshotはlockで保護する。

removable=falseでは開始前のinsertCardを必須とし、稼働中のinsertCard/removeCardを拒否する。
通知endpointとそのbufferを省く。停止後にはカードを変更できる。
着脱可能なreaderの通知要件と固定カードの違いは
[USB-IF CCID 1.1](https://www.usb.org/sites/default/files/DWG_Smart-Card_CCID_Rev110.pdf)の§3.1.2、§5.2に従う。

## APIと所有権

configure/onApdu/onEscape/onPowerはesp_err_tを返し、稼働中の変更を拒否する。
removeCardもesp_err_tを返す。戻り値を無視していた普通の呼出しは形を維持できる。
ESP_USB_DEVICE_CCID_BUFFER_SIZEの指定はコンパイル時に拒否し、configureへ移す。
messageCapacityは開始中の確保容量、開始前・終了後は0。configurationは要求値を返す。

APDU/Escape callbackはUSB workerで実行する。出力はcapacityを守り、使用byte数を返す。
bufferを保存して後から返信するAPIではなく、callbackからのUSB停止・再設定も行わない。
endではDeviceのworkerとDMAを停止してから状態を解放する。稼働中のFunction破棄もDevice停止を経る。

## transportの回復と残り

SET_INTERFACE alt 0はDMAを停止し、旧完了eventのfence後に両bulkと通知endpointをDATA0へ戻す。
未完message/未読返信と古いAbort待ちは破棄し、カード状態とapplication callbackは維持する。
class ABORTはslot/sequenceを記録し、対応するPC_to_RDR_Abortまで他のcommandや不一致sequenceを拒否する。
不正slot、容量超過、truncated messageには要求に対応する応答形式でエラーを返す。
32-bit長さfieldにheader長を加える計算は64-bitで行い、wraparoundを避ける。

任意のHALT/CLEAR_FEATUREと進行中message、bus reset/endとの全競合順序、PC/SCや各OS、
実カード・実readerとの相互接続、P4/S2/S31とHSはまだ検証対象として残る。


## S3–S3での確認

関連4件は527.51秒で成功し、CCIDを加えた774取得点の失敗/回復とheap一致を確認した。
最終のAbort状態補強後は、上限までのEscape、T=1パラメータとPowerOffを含むCCID境界1件が80.51秒で成功した。
実機のmessage容量は最大1088 byte、65535 byteはmetadata/確保/終了の確認であり、そのwire転送の合格ではない。
詳細とserial原本の所在は[独立試験の記録](../validation/README.ja.md#ccidのruntime容量と独立instance)。
