# v3 descriptorのサイズ照会と動的領域

2026-10-26に実装。descriptorの固定領域をやめ、構成から必要な長さを数えてから、実数分だけ確保する。

- 撤去した固定領域：configuration 704 byte、複合HID report 256 byte、BOS/MS OS。
- 確保した領域は、`begin()`の失敗時または`end()`で解放する。
- 通常のDevice/Function、`configure()`、Print/read/writeの使い方は変わらない。
- 独自Functionを書いている場合だけ、writerの移行が必要（[独自Functionのwriter契約](#独自functionのwriter契約)）。

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

- USBのwTotalLength/wDescriptorLengthが16 bitであるという上限は残る。これはUSB規格の制約で、TinyUSBの定数ではない。
- FSとHSで長さが違うFunctionも、速度ごとに照会・出力する。
  以前の「FS全体をHSへcopyして一部を上書きする」方法や、HIDの速度補正用の一時配列は不要になった。
- `begin()`で使う領域は、USB workerを起動する前に作って公開する。稼働中のGET_DESCRIPTORでは確保や作り直しをしない。
- FIFOの検証も、公開前の同じsnapshotを参照する。
- 確保に失敗したら`ESP_ERR_NO_MEM`を返し、途中までの構成は公開しない。

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

- FS専用機種でUSBを動かしている間は、HS領域がないのでHSの照会はnullptrを返し、`lastError()`が`ESP_ERR_NOT_SUPPORTED`になる。
- 停止中のpointer getterは必要なら作り直すので、次の構築や構成変更をまたいでpointerを保持しない。
- `begin()`で公開したpointerも、`end()`の後は使わない。

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

MS OS 2.0 descriptor setも、WinUSB対象（Vendor/DFU）の全Functionのinterfaceから作る。
以前の「Vendor/DFU各一個、最大二個」の一覧は使わない。

- Vendorを三つ使えば、三つのfunction subset、GUID property、revisionを、interfaceの順に出力する。
- 複数のWinUSB対象があるのに`msOs20Layout`でFlatを明示すると、`begin()`が`ESP_ERR_INVALID_ARG`で拒否する。
  一つのflat setに複数の対象を曖昧にまとめないため。
- 既定のAutoでは、configurationのinterfaceが2個以上ならfunction subsetsを選ぶ。
  `msOs20CcgpDevice = true`のときもsubsetsを選ぶ。単体Functionの従来の既定layoutとGUIDは変えない。

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
（2026-10-31追記：その後、P4/P4XのHS直結でHS descriptorの実列挙を確認した。[P4の記録](V3_P4_VARIANTS_AND_BENCH.ja.md)。）

文字列応答用のscratchとWebUSB URLの固定領域も、その後の実装で撤去した。
UTF-8入力、寿命、長さ制限の移行は[V3_TEXT_DESCRIPTORS.ja.md](V3_TEXT_DESCRIPTORS.ja.md)を参照する。
当時は「legacy class/core内の固定資源、残りのclass・EP0/DCDの独自化は継続する」としていた。
これらはその後すべて独自実装へ移した（[共通core](V3_CORE_API.ja.md)）。

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
（後にNCMを独自化し、MACをこの補助文字列でFunctionごとに持つようにした。[NCMガイド](V3_NCM_API.ja.md)。）
