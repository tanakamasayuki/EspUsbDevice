# v3 Function登録と構成の寿命

2026-10-26に実装。Functionの一覧を、固定6 slotから「Function自身が持つ登録link」と「`begin()`で作る一覧」に変えた。

- constructorでは一覧用のheapを確保しない。
- `begin()`で、実際のFunction数だけ検索用のpointer一覧を確保する。`end()`または開始失敗で解放する。
- `begin()`の前にdescriptorを明示照会した場合も、同じ大きさの一覧を必要なときに作る。
- Function名は[文字列descriptor](V3_TEXT_DESCRIPTORS.ja.md)として別に実数分だけ変換・保存する。元の名前を借用するpointer一覧は持たない。

## 普通の使い方

Deviceの後にFunctionを作り、設定してから`device.begin()`を呼ぶ。この流れは2.xと同じ。

```cpp
EspUsbDevice device;
EspUsbDeviceCdcSerial serial(device, "console");
EspUsbDeviceVendor data(device);

void setup() {
  if (!device.begin()) handleStartError(device.lastError());
}
```

- FunctionはconstructorでDeviceへ登録される。Deviceは登録の順番を保持する。
- DeviceはFunction本体を所有しない。DeviceはFunctionより長く生かす。
- Functionの追加・削除・設定変更は、停止中に行う。
- 登録済みのFunctionを再び`addClass()`しても、二重には登録しない。

## 失敗と拒否

| 状況 | 結果 |
| --- | --- |
| `begin()`で一覧の確保に失敗 | `begin()`がfalse、`lastError()`は`ESP_ERR_NO_MEM` |
| 稼働中にFunctionを追加しようとした | `ESP_ERR_INVALID_STATE`で拒否。そのFunctionは未登録のまま |
| 未登録のFunctionが残ったまま次の`begin()` | 拒否する。停止後に`addClass(&function)`で登録し直すか、停止後に作り直す |
| 拒否された未開始のCDC/Vendorを破棄 | 動いているDeviceは止めない |
| 未登録のHIDから送信 | 拒否する。登録済みFunctionのslotへ紛れ込ませない |
| Functionが251個を超える | `begin()`が`ESP_ERR_INVALID_SIZE` |

登録自体はheapを使わないので、constructorの中の確保失敗でFunctionが黙って抜け落ちることはない。

## 構成の上限

固定6個というビルド上の上限はなくなったが、任意の個数で通信できるわけではない。

- Function数の上限は251。文字列indexの割当て（Function名はindex 5〜255）による。
- その内側でも、USB interface、HID report ID、controllerのendpoint/FIFOの予算を`begin()`で検査する。
- 非controlのIN endpointは、S2/S3とP4 FSが4本、P4 HSが7本。
  たとえばS3では、2 CDCとCDC+Vendor+HIDは同時に置けない（別の構成になる）。

configuration、合成したHID report、BOS/MS OSの固定領域は、その後の実装で撤去した。
サイズ照会の契約と領域の寿命は[V3_DESCRIPTOR_STORAGE.ja.md](V3_DESCRIPTOR_STORAGE.ja.md)を参照。
HIDの速度別descriptorも直接出力するので、速度補正用の一時配列は使わない。

## HIDの複合とreport ID

以前は、同じ種類やcustomのHIDを複数置くと、同じreport IDを使ってしまった。
そこで構成の時点で、各Functionが希望するIDを先に予約し、重複したFunctionと指定のないFunctionへ空いているIDを割り当てる。
単一のFunctionや、重複しない既存の構成のIDは変わらない。

| API | 意味 |
| --- | --- |
| `hidReportId()` | Functionが希望するID。重複していればwireでは使わない |
| `compositeReportId()` | 構成で割り当てたID。複合でないとき、`end()`の直後は0。次の`begin()`かdescriptorの明示照会で割り当て直す |
| `sendReport`/`sendInput`/`sendUsage` | 割り当てたIDを使う |
| `HidCustom::sendReport`のreportId省略 | 割り当てに従う。複合で、割り当てと違うIDを明示すると拒否する |

- 利点：利用者が型ごとの固定IDや登録slotを管理しなくてよい。
- 不利益：複合でraw IDを決め打ちしていた通信相手は、descriptorからIDを読む必要がある。
- 未決：一つのcustom Functionが複数のReport IDを持つ形への一般化は、まだ整理していない。

## descriptorを公開する時期

- `begin()`の中で作ったdescriptorは、USB workerを起動する前に公開し、稼働中は作り直さない。
  GET_DESCRIPTORのたびに一覧やHIDの補正領域を確保し直さないため。
- 停止中の明示照会は作り直すことができる。構成を変えた後は、古い一覧やIDを使い回さない。
- 確保や構築に失敗したpointer getterはnullを返す。古いdescriptorを成功として返さない。
- 構成の数と登録状態は、`begin()`だけでなく、descriptorの単独照会でも検査する。

## 確認した範囲

新規`validation/s3/function_registry`で、S3–S3上で次を確認した。

- 7/8 Functionのdescriptorと名前、途中での削除と末尾への再追加、空の構成。
- 稼働中の登録拒否、停止後の明示的な再登録、二重登録がないこと、252個目の拒否。
- 上記を20回繰り返し、heapが元に戻ること。
- 7 HIDを一つのinterfaceにまとめ、7つのreport IDすべてのpayloadを実際に受信すること。
  同じ希望IDを二つ指定した場合の割り当て直しも含む。
- 稼働中に登録を拒否されたCDC/Vendor/HIDを破棄しても通信が続き、未登録のHIDからは送信できないこと。
- 一覧を含むVendor/CDCの確保点62か所で、失敗と回復を確認した。

既存のtest/sampleを合否の基準にせず、conftestや独自のpytest hookは追加していない。

| 実行 | 結果 |
| --- | --- |
| 標準の全体実行`dynamic-registry-all.xml` | 7 passed、360.78秒 |
| 停止中のdescriptor照会と登録拒否`function-registry-final.xml` | 1 passed、31.65秒 |
