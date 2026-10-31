# v3のUSB文字列とWebUSB URL

文字列はUTF-8で受け取り、`begin()`のときにUSB用のUTF-16へ変換して保存する（2026-10-26に実装）。

2.xからの変更点：

- 63 byteでの切り詰めをやめた。
- UTF-8の各byteをそのままUTF-16へ広げる処理（日本語が文字化けする原因）をやめた。
- WebUSB URLを128 byteの共通領域で作り直す処理をやめた。
- 設定の書き方（Device configとFunction名）は変えていない。

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

- 入力はNUL終端のUTF-8。`begin()`が戻るまで、有効で変更しない配列を渡す。
- `begin()`が成功した後は、descriptor用のcopyを使う。元の配列を書き換えてもUSBの応答は変わらない。
- 次の`begin()`には、その時点で有効な入力を設定し直す。Functionの`setName`も停止中に行う。
- `config()`は渡したpointerの値を返すだけで、元の配列の寿命を延ばさない。

### 既定と省略

| 項目 | 既定 | null | 空文字 |
| --- | --- | --- | --- |
| `manufacturer` | `"EspUsbDevice"` | 省略（index 0） | 長さ0の文字列descriptor |
| `product` | `"EspUsbDevice"` | 省略 | 長さ0の文字列descriptor |
| `serialNumber` | nullptr | 省略 | 長さ0の文字列descriptor |
| Function名 | Functionごと | 省略 | 省略 |

- 文字列は、指定されたものだけを一つの領域に保存する。indexからの位置の表も実数分だけ確保する。
- すべて省略すれば、文字列用のheapは確保しない。index 0（LANGID）の応答は4 byteの定数。
- NCMのMAC文字列は、NCM Functionがある場合だけ含める（[Function固有の補助文字列](V3_DESCRIPTOR_STORAGE.ja.md#function固有の補助文字列)）。
- 文字列のindexは、1〜3がmanufacturer/product/serial、4が予約、5以降がFunction名、その後にFunction固有の補助文字列。

### 長さと不正な入力

| 条件 | 結果 |
| --- | --- |
| 1文字列が126 UTF-16 code unitを超える | `begin()`が`ESP_ERR_INVALID_SIZE` |
| 不正なUTF-8 | `begin()`が`ESP_ERR_INVALID_ARG` |

- 126 unitの目安：ASCIIだけなら126文字。絵文字などの補助文字は1文字で2 unitを使う。
- headerを含めて最大254 byteになり、USBの1 byteの長さfieldに収まる。
- 不正なUTF-8には、overlong、surrogateの値、U+10FFFF超、途中で切れた列を含む。
  replacement文字への置換や切り詰めはしない。
- LANGIDは0x0409の一つだけ。`stringDescriptor()`の直接照会で`langid=0`を渡しても同じ文字列を返す。他のLANGIDには応答しない。

### pointerの有効期間

- `begin()`で公開した`stringDescriptor()`のpointerは、他の文字列を照会しても上書きされず、`end()`まで有効。
- 停止中の明示照会は、descriptorを作り直すことがある。次の構築をまたいで保持しない。
- `end()`または開始失敗で、すべての領域を解放する。GET_DESCRIPTORの処理中に変換や確保はしない。

## WebUSB

`webusbEnabled = true`でBOSのWebUSB capabilityを有効にする。

| `webusbUrl` | 動作 |
| --- | --- |
| nullptr | landing pageなし（iLandingPage=0）。URL用の領域は確保しない |
| `https://…` | scheme 1。prefixを除いて保存する |
| `http://…` | scheme 0。prefixを除いて保存する |
| それ以外 | scheme 255。全文をそのまま保存する |
| 空文字、または`http://`/`https://`だけ | `begin()`が`ESP_ERR_INVALID_ARG` |

- 保存できるのは、prefixを除いて最大252 byteのUTF-8。超えると`ESP_ERR_INVALID_SIZE`、不正なUTF-8は`ESP_ERR_INVALID_ARG`、確保失敗は`ESP_ERR_NO_MEM`。
- URL全体の構文や、アクセスできるかは検証しない。
- 応答するのは、device宛のIN vendor request（0xc0）、GET_URL（wIndex=2）、URL index 1だけ。
  不正なindex/方向/recipientはSTALLし、その後の正しい要求は続けて受け付ける。
- `webUsbUrlDescriptor()`は構築済みのbyte列かnullを返す。`end()`または開始失敗で無効になる。

### 2.xからの移行

- 2.xではprefixのないURLを暗黙にhttpsとして扱っていた。httpsにしたいなら`https://`を明記する。
- URLを黙って125 byteで切り詰める処理もやめた。長すぎる設定は開始エラーになる。
- 短いhttps/http URLを明記する普通の設定は変わらない。

## 判断の理由

- 利点：日本語や絵文字を正しく出せる。長い名前を使える。稼働中に元の配列へ依存しない。使わないURLや文字列の領域を確保しない。
- 不利益：`begin()`での変換と確保点が増える。以前は切り詰めや文字化けで通っていた設定が、明示的なエラーになる。
- 移行点：入力をUTF-8で渡すこと。長さと`begin()`の結果を確かめること。

## 検証の記録

S3–S3のfocused試験`text-descriptors.xml`：2 passed、89.45秒。

- 日本語/絵文字、最大254 byteの文字列と255 byteのURL descriptorのwire応答。
- 短い要求、無効な要求の後の復帰、元配列を変えた後の再取得。
- 各種の不正UTF-8と長さの境界、10回のheap回復、107取得点の確保失敗と回復。

その後の標準`--combo all`（`immutable-text-all.xml`）：8 passed、0 skipped、0 unfinished、408.43秒。
文字列なしで確保しないことを含む118取得点の失敗/回復と、251 Functionの長い名前・index 255も確認した。
全caseで両board STOPPED、panicなし。Device only / Vendor copy / CDC copyの3つのbuildとnmも成功し、
未使用classと取消し処理がリンクされないことを確かめた。

この結果は、多言語の翻訳、Windowsやブラウザでの表示、P4/S2/S31の実機動作までは示さない。

参照：[USB文字列](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/usb-string-descriptors)、
[USB_STRING_DESCRIPTOR](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/usbspec/ns-usbspec-_usb_string_descriptor)、
[WebUSB GET_URLとURL descriptor](https://wicg.github.io/webusb/#get-url)。
