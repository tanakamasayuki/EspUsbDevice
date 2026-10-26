# v3のUSB文字列とWebUSB URL

2026-10-26。Device configとFunction名の入力方法を維持し、UTF-8からUSB文字列へ変換する。
begin時に必要量だけ確保し、稼働中は変更しない。旧63 byteの切り詰めと、各UTF-8 byteを
そのままUTF-16へ広げる処理を廃止した。WebUSB URLも128 byteの共通領域で作り直さない。

## 設定と寿命

```cpp
EspUsbDevice device;
EspUsbDeviceCdcSerial console(device, "計測ポート");

void setup() {
  EspUsbDeviceConfig config;
  config.manufacturer = "計測機器";
  config.product = "USB Console 🧪";
  config.serialNumber = "UNIT-001";
  if (!device.begin(config)) handleStartError(device.lastError());
}
```

入力はNUL終端のUTF-8。毎回のbeginが戻るまで、有効で変更されない配列を渡す。
成功したbeginの後はdescriptor用のcopyを保持するため、元の配列を書き換えてもUSB応答は変わらない。
次回のbeginには、その時点で有効な入力を設定し直す。FunctionのsetNameも停止中に行う。
`config()`は渡したpointer値を保持する既存の参照APIで、元の配列の寿命を延ばすものではない。

manufacturer/product/serialのnullは省略、空文字は空のdescriptorとなる。
Function名は従来どおりnull/空文字なら省略する。
USB文字列は提供されたものだけを一つの領域に保存し、indexへのoffset表も実数分だけ確保する。
全て省略すれば文字列用heapを確保しない。index 0のLANGID応答は4 byteの定数。
NCMのMAC文字列はNCM Functionがある場合だけ含める。

各文字列の上限は126 UTF-16 code unit。ASCIIだけなら126文字、補助文字の絵文字は通常2 unitを使う。
header込みで最大254 byteとなり、USBの1 byte長フィールドに収まる。
長さ超過は`ESP_ERR_INVALID_SIZE`、不正なUTF-8は`ESP_ERR_INVALID_ARG`でbeginを失敗させる。
overlong、surrogate値、U+10FFFF超過、不完全な列も拒否し、replacement文字への置換や切り詰めは行わない。
LANGIDは従来の0x0409一つ。直接照会の`langid=0`も同じ文字列を返すが、他のLANGIDは応答しない。

beginで公開した`stringDescriptor()`のpointerは、同じ構成の他の文字列照会では上書きされず、endまで有効。
停止中の明示照会は他のdescriptorと同じく再構築し得るため、次の構築を跨いで保持しない。
end/開始失敗で全領域を解放する。GET_DESCRIPTOR中のUTF変換・確保はない。

## WebUSB

`webusbEnabled=true`でBOS capabilityを有効にする。`webusbUrl=nullptr`ならlanding pageなし
（iLandingPage=0）とし、URL用領域を確保しない。URLを指定した場合はindex 1のdescriptorを一度構築する。
空文字または`http://`/`https://`だけの入力はInvalidArgument。

`https://`はscheme 1、`http://`はscheme 0としてprefixを除き、他の入力はscheme 255で全文を保存する。
prefix除去後で最大252 byteのUTF-8を保持できる。超過・不正なUTF-8・確保失敗はbeginのエラーとなる。
URL全体の構文やアクセス可能性の検証は行わない。

従来prefixなしURLは暗黙にhttps扱いだった。httpsを期待する既存コードは`https://`を明記する。
URLの黙った125 byte切り詰めも廃止したため、長すぎる設定を使うコードは開始エラーを扱う必要がある。
通常の短い明示https/http URLの設定方法は変わらない。

応答はdevice宛IN vendor request（0xc0）、GET_URL（wIndex=2）、URL index 1だけを受け付ける。
不正なindex/方向/recipientをSTALLし、その後の正しい要求は継続できる。
`webUsbUrlDescriptor()`は構築済みのconst byte列またはnullを返し、end/開始失敗で無効になる。

## 判断と検証範囲

利点は日本語・絵文字の正しい出力、長い名前、稼働中の元配列への依存解消、使わないURL/文字列の確保省略。
不利益はbegin時の変換と確保点、および以前は切り詰めや文字化けで通っていた設定の明示エラー化。
入力をUTF-8で渡すこと、長さと開始結果を確認することが既存利用の移行点になる。

S3–S3のfocused試験`text-descriptors.xml`は2 passed、89.45秒。
日本語/絵文字、最大254 byte文字列と255 byte URLのwire応答、短い要求、無効な要求後の復帰、
元配列の変更後の再取得、各種不正UTF-8と長さ境界、10回のheap回復、107取得点の確保失敗/回復を確認した。
この結果は多言語翻訳、Windows/ブラウザでの表示、P4/S2/S31の実機動作までは証明しない。

その後の標準`--combo all`実行`immutable-text-all.xml`は8 passed、0 skipped、0 unfinished、408.43秒。
文字列なしの確保省略を含む118取得点の失敗/回復と、251 Functionの長い名前・index 255も確認した。
全caseで両board STOPPED、panicなし。Device only / Vendor copy / CDC copyの3 build/nmも成功し、
未使用クラスと取消し処理のリンク除外を維持した。

参照: [USB文字列](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/usb-string-descriptors)、
[USB_STRING_DESCRIPTOR](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/usbspec/ns-usbspec-_usb_string_descriptor)、
[WebUSB GET_URLとURL descriptor](https://wicg.github.io/webusb/#get-url)。
