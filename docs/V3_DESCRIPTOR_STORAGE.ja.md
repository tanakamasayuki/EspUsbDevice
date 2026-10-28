# v3 descriptorのサイズ照会と動的領域

2026-10-26。configurationの704 byte、複合HID reportの256 byte、BOS/MS OSの固定領域を撤去した。
構成を数えてから実数分だけ確保し、begin失敗またはendで解放する。
通常のDevice/Function、configure、Print/read/writeの使い方は変えない。

## 使用した分だけ確保する

| 領域 | 確保する条件 |
| --- | --- |
| FS configuration | descriptor構築時。9 byteのheaderと、選択したFunctionの実際の長さ |
| HS configuration | HSを使うcontrollerではbegin時。FS専用機種では停止中の明示照会時のみ |
| Other speed応答 | HS controllerが必要とする場合。FS/HSの大きい方の実際の長さ |
| 複合HID report | HIDを複合する場合のみ。単体HIDはFunctionの既存descriptorを参照 |
| MS OS 2.0 set | WinUSB対象Functionがある場合のみ |
| BOS | WebUSBまたはMS OS capabilityを使う場合のみ |
| USB文字列 | 提供した文字列のUTF-16とindexまでのoffset表。全てnullなら確保なし |
| WebUSB URL | WebUSB有効かつURLありの場合のみ。prefix除去後の実byte数+3 |

USBのwTotalLength/wDescriptorLengthが16 bitである制約は残る。これはTinyUSBの転送buffer定数ではない。
FS/HSの長さが違うFunctionも、速度ごとに照会・出力する。以前のFS全体をHSへコピーして
一部だけ上書きする方法や、HIDの速度補正用一時配列も不要になった。

beginで使う領域はUSB workerの起動前に構築・公開する。稼働中のGET_DESCRIPTORは確保や再構築を行わない。
FIFOの検証も同じ公開前のsnapshotを参照する。確保失敗はNoMemoryとして返し、一部だけの構成を公開しない。

FS専用機種でHSを検討する場合は、PHYを開始しない構成で明示照会できる。

```cpp
EspUsbDeviceConfig config;
config.startMode = EspUsbDeviceStartMode::DescriptorsOnly;
if (!device.begin(config)) return;
const uint8_t *fs = device.configurationDescriptor(0);
const uint8_t *hs = device.configurationDescriptorForSpeed(0, true);
// Inspect/copy while this configuration is alive.
device.end();
```

実USB稼働中のFS専用機種では、HS領域がなければHS照会はNotSupportedになる。
停止中のpointer getterは必要なら再構築するため、次の構築や構成変更を跨いでpointerを保持しない。
beginで公開したpointerもend後には保持しない。

## 独自Functionのwriter契約

独自Functionを実装している場合は移行が必要。`dst=nullptr`をサイズ照会として扱い、書き込みを行わない。
`configurationDescriptorForSpeed()`をoverrideする場合も同じ契約で、非null時はcapacityを守る。
設定を変更するまで、照会値と出力長を一致させる。

```cpp
uint16_t configurationDescriptor(uint8_t *dst, uint8_t itf,
                                 uint8_t ep, uint16_t packet) override {
  const uint8_t bytes[] = {
    9, 4, itf, 0, 0, 0xff, 0, 0, 0
  };
  if (dst) memcpy(dst, bytes, sizeof(bytes));
  return sizeof(bytes);
}
```

既定のForSpeed wrapperは、旧形式のwriterにも先にサイズ照会し、容量不足なら書き込まない。
Audio等のbuilderでは内部DescriptorBufferのnull/0による計数を使い、出力と同じ経路で長さを求める。
短いcapacityで失敗した場合にbuffer内容を未変更で保つ契約ではなく、境界を越えて書かない契約である。
書き込み前の計数から長さが変わった場合、Deviceも出力先の残量を再検証して拒否する。

利点は正確な確保量と、無関係な構成の固定最大値を利用者が調整しなくてよいこと。
不利益はbegin時の計数・確保点が増えることと、旧独自writerへnull照会対応が必要になること。
書込み先なしでもmemcpyする旧実装をそのまま使わず、上記契約へ移行する。

## 複数のWinUSB Function

MS OS setも全対象Functionのinterfaceから構築する。以前のVendor/DFU各一個、最大二個の一覧は使わない。
Vendorを三つ使えば三つのfunction subset、GUID property、revisionを出力し、interface順に並べる。
一つのflat setに複数対象を曖昧にまとめないため、複数WinUSB対象でFlatを明示するとInvalidArgumentで拒否する。
通常のAutoではfunction subsetsを選ぶ。単体Functionの従来の既定layoutとGUIDは維持する。

## S3–S3の実験

新規`validation/s3/descriptor_storage`を使い、旧test/sampleをgateにせず確認した。

- 22種/設定のFunctionについてFS/HS計44条件。サイズ照会、短いcapacityの拒否、出力長、全TLV境界、canary。
- 20回の不安定なサイズ申告/長さ超過の拒否、停止中HS照会、heap回復。
- configuration 846 byteとMS OS set 516 byte、三つのWinUSB subset/property/revision。
  S3 Hostの実列挙後、1/63/64/65/127/128/129 byteのVendor往復。
- 複合HID report 366 byteをHostが読み、両report IDのpayloadを受信。
- 不要なBOS/MS OSはpointerも長さも0。86取得点の確保失敗と回復。

最終の標準入口は`dynamic-descriptors-all.xml`、8 passed、390.92秒。
HS byte列の検証をS3上で行った結果であり、P4/S2/S31のPHY・DMA/cacheやWindowsのWinUSB動作を検証したものではない。

文字列応答用scratchとWebUSB URLの固定領域も、その後の実装で撤去した。
UTF-8入力、寿命、長さ制限の移行は[V3_TEXT_DESCRIPTORS.ja.md](V3_TEXT_DESCRIPTORS.ja.md)を参照する。
legacy class/core内の固定資源、残クラス・EP0/DCDの独自化は継続する。

追加のfocused実行は`descriptor-storage-final.xml`、1 passed、28.45秒。
WebUSBのみのBOS 29 byte、MS OS併用57 byte、誤ったGUIDの拒否、
FS18/HS27 byteと長さが違う独自Function、HS bulkの512 byteも確認した。
Device only / Vendor copy / CDC copyの再compile/nmでも未使用クラス・取消しのリンク除外を確認した。


## Function固有の補助文字列

2026-10-27。NCMのMAC文字列をFunction単位へ移す前提として、EspUsbDeviceClassに
auxiliaryStringCount/auxiliaryString/assignAuxiliaryStringIdsを追加した。
Function名の後へ連続したindexを割り当て、既存のexact UTF-16 snapshotへ一緒に保存する。
使用しないFunctionの追加領域はなく、既存の名前indexと251 Functionの上限を保つ。
countとtextはbeginの処理中に安定していることがwriter契約であり、宣言した文字列のnullptrは拒否する。
稼働中のUSB応答はsnapshotなので元配列の変更に依存しない。

`function-auxiliary-strings.xml`は**3 passed、0 skipped、0 unfinished、138.04秒**。
複数Functionの別領域、空の名前、index255、超過/SIZE_MAX/null/不正UTF-8からの回復、
実USB上の名前5と補助6/7・配列変更後の応答、既存registryと502取得点の失敗/回復を確認した。
両board STOPPED、panicなし。serial原本は`/tmp/pytest-embedded/2026-10-26_19-51-05-602028/`、コピーとJUnit/transcriptは
`validation/.pytest-results/function-auxiliary-strings*`。
通常収集は17件のまま。今回の関連3件は、DFU段階の全17件成功とは別の結果である。
NCMのclass/NTB pool/MAC globalはまだlegacyのままで、この変更だけで複数NCM通信に対応したとはしない。
