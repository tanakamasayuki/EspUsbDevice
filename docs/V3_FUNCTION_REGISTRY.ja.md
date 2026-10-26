# v3 Function登録と構成の寿命

2026-10-26。Function一覧を固定6 slotから、Function自身の登録リンクとbegin時の動的一覧へ変更した。
constructorで一覧用heapを確保しない。beginで実際のFunction数だけ検索用pointerと名前の一覧を確保し、
endまたは開始失敗で解放する。descriptorをbegin前に明示照会した場合も、同じサイズの一覧を必要時に作る。

## 通常の使い方

Deviceより後でFunctionを作り、設定してからdevice.begin()を呼ぶ流れは維持する。
FunctionはconstructorでDeviceへ登録し、Deviceは登録順を保持する。DeviceはFunction本体を所有しない。
Functionの追加・削除・設定変更は停止中に行い、DeviceはFunctionより長く保持する。
登録済みFunctionを再びaddClassしても重複登録しない。

```cpp
EspUsbDevice device;
EspUsbDeviceCdcSerial serial(device, "console");
EspUsbDeviceVendor data(device);

void setup() {
  if (!device.begin()) handleStartError(device.lastError());
}
```

一覧の確保失敗はbeginのESP_ERR_NO_MEMとして返る。登録自体にOOMがないため、
constructor内の確保失敗でFunctionが黙って脱落する形にはしない。
稼働中に追加しようとした場合はESP_ERR_INVALID_STATEで拒否し、そのFunctionが未登録のままなら
後のbeginも拒否する。停止後にaddClass(&function)で再登録するか、停止後に作り直す。
拒否された未開始のCDC/Vendorを破棄しても、動いているDeviceを停止しない。
未登録HIDからの送信も拒否し、登録済みFunctionのslotへ紛れ込ませない。

## 構成の制限

固定6個というビルド上限は廃止したが、任意の個数が通信可能になるという意味ではない。
現時点のFunction identity/string indexの割当では251個が上限で、超過はbeginでInvalidSizeになる。
その内側でもUSB interface/report IDとcontroller endpoint/FIFOの予算を検証する。
S3の非制御IN endpointは4本であり、2 CDCとCDC+Vendor+HIDは別構成になる。

configuration descriptorの固定704 byte、merged HID reportの256 byte、BOS/MS OS等の固定領域は
まだ撤去していない。一覧の動的化だけでそれらの制約を解決したとは扱わない。
HIDの速度補正用の一時領域はHIDがある時だけ確保し、固定6個のstack配列を使わない。

## HIDの複合identity

同じ種類やcustomのHIDを複数置くと、従来は同じreport IDを使ってしまった。
構成時に一意なpreferred IDを先に予約し、重複したFunctionと未指定のFunctionへ空きIDを割り当てる。
通常の単一Functionや重複しない既存構成のIDは維持する。

- hidReportId()はpreferred ID。重複時にそのままwireへ使う値ではない。
- compositeReportId()は構成で割り当てた複合ID。非複合やend直後は0。次のbeginまたはdescriptor明示照会で再割当する。
- 通常のsendReport/sendInput/sendUsageは割当済みIDを使う。
- HidCustom::sendReportの省略reportIdも割当に従う。複合で明示したIDが割当と違う場合は拒否する。

これにより利用者が型ごとの固定IDや登録slotを管理する必要を減らす。
一方、複合でraw IDを決め打ちしていた通信相手は、descriptorのIDを読む必要がある。
複数Report IDを内包する一つのcustom Functionまで一般化した設計は、HID独自化で引き続き整理する。

## descriptorの公開

begin中に作ったdescriptorは、USB workerを起動する前に公開し、稼働中は作り直さない。
GET_DESCRIPTORのたびに一覧やHID補正領域を再確保することを避ける。
停止中の明示照会は再構築できる。構成変更後は古い一覧・identityを使い回さない。
確保/構築に失敗したpointer getterはnullを返し、古いdescriptorを成功として返さない。

## 確認した範囲

新規`validation/s3/function_registry`で以下をS3–S3上で確認する。

- 7/8 Functionのdescriptorと名前、途中削除と末尾への再追加、空構成。
- 稼働中の登録拒否、停止後の明示再登録、二重登録なし、252個の拒否。
- 上記を20回反復し、heapが戻ること。
- 7 HIDを一つのinterfaceにまとめ、全7 report IDのpayloadを実際に受信すること。
  同じpreferred IDを二つ指定した場合の再割当も含む。
- 稼働中に登録拒否されたCDC/Vendor/HIDの破棄後も通信が継続し、未登録HIDは送信できないこと。

一覧を含むVendor/CDCの取得点は62か所の失敗・回復を確認する。
既存test/sampleをgateにせず、conftestや独自pytest hookは追加していない。

最終の標準実行は`dynamic-registry-all.xml`、7 passed、360.78秒。
追加の停止中descriptor照会・登録拒否条件は`function-registry-final.xml`、1 passed、31.65秒で確認した。
構成の数/登録状態はbeginだけでなく、descriptor単独照会でも検証する。
