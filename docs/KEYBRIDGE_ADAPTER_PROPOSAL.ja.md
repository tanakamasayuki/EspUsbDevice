# EspUsbDevice NKRO 状態送信 API / output report listener 仕様案

## 目的

`EspUsbDeviceHidKeyboard` に、保持キー全体を 1 回で送る公開 API を追加する。あわせて Host からの LED 状態をスケッチ側から観測できるようにする。

この仕様は、統合ライブラリ `/home/mt/dev/ESP32KeyBridge/` からの要求を受けて定義する。ESP32KeyBridge は毎周期に「今押されているキー集合」全体を出力 adapter へ渡す状態ベースの設計で、現在の公開 API はこの形と噛み合わない。

## 前提: API 形状は確定済み（EspBle / EspUsbHost 側の対応完了）

NKRO の項目は姉妹ライブラリ EspBle と**共通の設計判断**だが、EspBle 側は**既に実装・peer テスト済み**である（`EspBleHidKeyboardNkroReport` / `sendReport()` overload、`docs/HID_DEVICE_SPEC.ja.md` の HID keyboard 節）。かつてこの文書が参照していた `EspBle/docs/PROPOSAL_KEYBRIDGE_ADAPTER.ja.md` は実装完了に伴い削除されており、もう存在しない。

そのため本件は「両ライブラリで新規に設計する」話ではなく、**EspBle の実装を基準に EspUsbDevice を対称に作る**話になる。リリース前に直すべき点は `docs/ESPBLE_NKRO_API_CHANGE_REQUEST.ja.md` として EspBle へ依頼し、**A〜E すべて対応済み**（1.0.0 未リリースのうちに破壊的変更を含めて実施された）。したがって**この提案の実装ブロッカーは無い**。

確定した形（EspBle 側の現物、`src/EspBle.h` / `src/EspBle.cpp`）:

- struct メンバは `modifiers` + `bitmap[28]`（`keys` ではない）
- 内部保持状態は struct 型のメンバ 1 個（`nkroState_`）、全 NKRO 送信経路を private `sendHeldNkroState()` に集約
- `heldState()` で Host へ最後に伝えた状態を公開。重複送信の抑制はライブラリ側でやらない
- `enableNkro()` 未実行の NKRO 送信は失敗、Boot Protocol 時は畳んで送る（`docs/DECISIONS.ja.md` 21-22）
- 増分 API（`pressUsage()` / `releaseUsage()`）のエラーは struct の `press()` / `release()` の戻り値から再構成する（同 20）

**命名規則は姉妹 3 ライブラリ共通の決定事項になった**（`EspBle/docs/DECISIONS.ja.md` 19）。bitmap を持つメンバは `bitmap`、usage の配列を持つメンバは `keys`。`keys[0] = 0x04` が型によって「usage 0x04 が押されている」と「usage 3 と 5 が押されている」の別の意味になり、取り違えてもコンパイルが通るため。EspUsbHost も `EspUsbHostKeyboardState` を `bitmap` / `changedBitmap` へ改名して 2.7.0 でリリース済み。EspUsbDevice で bitmap を持つ公開メンバは本 struct が最初なので、`bitmap` で作れば既存 API の改名は要らない（boot report の `keys[6]` は usage 配列なので現状のままで正しい）。

## 項目①: NKRO の状態送信 API

### 現状

`enableNkro()` で NKRO を有効にできるが、**bitmap へ到達する公開経路が増分 API しかない**。

```cpp
// EspUsbDevice.cpp
bool EspUsbDeviceHidKeyboard::sendReport(const EspUsbDeviceBootKeyboardReport &report, uint32_t timeoutMs)
{
  if (nkroEnabled_)
  {
    // Adopt the supplied 6-key report as the full held-key state, then emit it in
    // whatever format the active protocol needs (NKRO bitmap or boot fallback).
    nkroModifiers_ = report.modifiers;
    memset(nkroBitmap_, 0, sizeof(nkroBitmap_));
    for (size_t i = 0; i < sizeof(report.keys); i++)
      if (report.keys[i]) setKeyBit(report.keys[i], true);
    return sendNkroReport(timeoutMs);
  }
  ...
}
```

- 公開 `sendReport()` が受け取るのは `keys[6]` の `EspUsbDeviceBootKeyboardReport`。**NKRO を有効にしても 1 回の送信で表現できるのは 6 キーまで**
- 内部 bitmap は 28 byte（usage `0x00`〜`0xDF`）あり容量は足りているが、`sendNkroReport()` は private
- 7 キー以上を押すには `pressUsage()` / `releaseUsage()` を順に呼ぶしかない

### 要求元の状況

ESP32KeyBridge の `OutputAdapter::write(const KeySet &keys)` は、変換後のキー集合**全体**を毎周期受け取る契約になっている。増分 API に合わせると次の問題が出る。

- adapter 側で前回状態との差分計算と、ライブラリ内部状態との同期が必要になる
- ライブラリ側 `nkroBitmap_` と adapter 側の期待状態がずれた場合、`releaseAll()` による全消し以外の復旧手段がない
- 1 キーの変化ごとに 1 report となり、同時押し・同時離しが分割される

結果として `src/ESP32KeyBridgeEspUsbDevice.h` の出力 adapter は `EspUsbDeviceBootKeyboardReport` しか使っておらず、**ESP32KeyBridge 経由では USB で NKRO を出せない**。ESP32KeyBridge 側には最大 32 キーを組む `buildHidKeyboardRolloverReport()` があるが、出す先が無いためどの adapter からも使われていない。

「両ライブラリとも NKRO 対応済み」なのに bridge 経由では 6KRO に落ちる、という状態になっている。

### 提案 API

```cpp
// Full NKRO keyboard state in one report: modifier byte + a bitmap of usages
// 0x00-0xDF. Modifier usages 0xE0-0xE7 live in `modifiers`, not the bitmap, and
// press() / release() route them there automatically.
struct EspUsbDeviceNkroKeyboardReport
{
  static constexpr size_t BitmapSize = 28;
  static constexpr uint8_t MaxBitmapUsage = 0xdf;

  uint8_t modifiers = 0;
  // A bitmap, not an array of usages: bit (usage & 7) of bitmap[usage >> 3].
  // Deliberately NOT named `keys`, which in the boot report means a 6-entry
  // usage array — the two are indexed differently and must not read alike.
  uint8_t bitmap[BitmapSize] = {};

  void clear();
  // Returns false when the usage is above MaxBitmapUsage and is not a modifier
  // (0xE0-0xE7), i.e. this report cannot represent it.
  bool press(uint8_t usage);
  bool release(uint8_t usage);
  bool isDown(uint8_t usage) const;
};

class EspUsbDeviceHidKeyboard {
public:
  // Sends the whole held-key state as one report. Requires enableNkro() before
  // EspUsbDevice::begin(); fails otherwise (see "未有効時は失敗させる" below).
  // Falls back to the 6-key boot format when the host selected boot protocol,
  // exactly like the existing path. The boot-report sendReport() overload stays
  // valid and keeps working.
  bool sendReport(const EspUsbDeviceNkroKeyboardReport &report, uint32_t timeoutMs = 100);

  // The state the host was last told about (NKRO only; meaningless when NKRO is
  // off). Lets a state-based caller resynchronise or diff without keeping a
  // shadow copy, and removes the "releaseAll() is the only recovery" problem.
  const EspUsbDeviceNkroKeyboardReport &heldState() const;
};
```

`sendReport()` の戻り値は「送信できたか」であり、`press()` の戻り値は「その report で表現できるか」である。後者は送信前に呼び出し側でチェックできる。

### 実装方針

**1. 内部状態を struct そのものにする。** `nkroModifiers_` / `nkroBitmap_[28]` の 2 メンバを廃止し、`EspUsbDeviceNkroKeyboardReport nkroState_` 1 個に置き換える。理由は 2 つ。

- private `setKeyBit()`（`EspUsbDevice.cpp`）と struct の `press()` / `release()` が同じビット演算を二重に持つのを防ぐ。ビット演算の正解を 1 箇所にする
- `heldState()` が内部状態の参照を返すだけで済む（コピーもフィールド組み立ても不要）

`setKeyBit()` は削除し、呼び出し側（`pressUsage()` / `releaseUsage()` / 6 キー版 `sendReport()`）を `nkroState_.press()` / `.release()` へ書き換える。`sendNkroReport()` / `releaseAll()` / boot fold-down はいずれも `nkroState_.modifiers` と `nkroState_.bitmap` を読むだけになる。EspBle が同じ整理を済ませており（`nkroState_` + 送信経路を `sendHeldNkroState()` 1 本に集約）、そちらに倣う。

なお `setKeyBit()` は範囲外 usage を**黙って無視**している（`usage > 0xdf` で早期 return）。`press()` / `release()` は false を返すので、`pressUsage()` / `releaseUsage()` は戻り値から失敗を返すようにする（EspBle も `InvalidArgument` を struct の戻り値から再構成する形にした。`EspBle/docs/DECISIONS.ja.md` 20）。これは増分 API の挙動変更なので CHANGELOG に書く。

**2. 送信経路は増やさない。** 新 overload は `nkroState_` を丸ごと置き換えて既存の private `sendNkroReport()` を呼ぶ。boot protocol 選択時のフォールバックは `sendNkroReport()` が既に持っているため流用できる。**実質は private 関数を状態受け取り型の公開 API として出す作業**。

**3. `pressUsage()` / `releaseUsage()` との併用時も一貫する。** 同じ `nkroState_` を触るため。

**4. 未有効時は失敗させる（fold-down しない）。** `enableNkro()` 未実行での呼び出しは失敗させる。boot protocol 時の fold-down との違いは意図的で、判断基準は次のとおり。

- **boot protocol は Host 主導の実行時条件**。スケッチに責任は無く、送れる形に畳んで送るのが正しい
- **`enableNkro()` 忘れは設定ミス**。畳んで成功させると 7 キー目以降が恒久的に無言で消える。即座に気付ける失敗のほうが良い

呼び出し側は既に public な `nkroEnabled()` で事前判定できるので、失敗設計でも「送れるか」のクエリが不足することはない（EspBle 提案①相当の穴は開かない）。EspBle も同じ理由で `InvalidState` 失敗を選び、peer テストで固定済み。**この非対称は README / DESIGN_NOTES に理由込みで明記する**（書かないと後から「バグ」として再燃する）。

**5. 同一状態の連投はライブラリ側で抑制しない。** 要求元は毎周期 `write(KeySet)` を呼ぶ契約なので、状態が変わらない周期でも `sendReport()` が来る。ライブラリ側で「前回と同じなら送らない」を隠し持つと、`releaseAll()` や boot protocol 切替を挟んだときの再同期が読めなくなる。**無条件送信とし、重複抑制は adapter 側の責務**と明記する。`heldState()` があるので adapter は shadow copy 無しで比較できる。

**6. boot fold-down の現挙動を「仕様」として明文化する。** 現実装（`EspUsbDevice.cpp` の `sendNkroReport()`, `protocol_ == 0` 分岐）は次のとおりで、これを変えずに文書化する。

- 押した順ではなく **usage 番号の小さい順**に先頭 6 個を採る
- 7 キー以上でも HID 的に正しい `ErrorRollOver`(`0x01`) は返さない

これは妥協の固定である。boot protocol は BIOS / UEFI 用で、そこで 7 キー同時押しを識別させる要求が実用上ほぼ無い、というのが根拠。**根拠まで書く**こと。

**7. 併せて既存の穴を直す（`releaseUsage()` の modifier）。** 現状 `EspUsbDeviceHidKeyboard::releaseUsage()` は bitmap のビットを落とすだけで `nkroModifiers_` を触らないため、modifier usage（`0xE0`〜`0xE7`）を離しても `releaseAll()` まで押されたままになる。EspBle 側の `releaseUsage()` は modifier を落としており、**この点は既に非対称**。state API は構造的にこれを回避するが、増分 API 側も EspBle に合わせて修正する。回帰テストを付ける。

### EspBle との対称性（重要）

EspBle の 6 キー版 `sendReport()` は本ライブラリと同じ構造・同じ 6 キー制限を持つ（EspBle が「EspUsbDevice 互換の 29-byte bitmap」として作られているため、制限まで継承している）。NKRO 版 overload は EspBle 側に既にある。

確定させたい最終形（EspBle への依頼が通った後の姿）:

| | EspUsbDevice | EspBle |
|---|---|---|
| struct | `EspUsbDeviceNkroKeyboardReport` | `EspBleHidKeyboardNkroReport` |
| `BitmapSize` | 28 | 28 |
| `MaxBitmapUsage` | `0xdf` | `0xdf` |
| メンバ | `modifiers` / `bitmap[]` | 同左（対応済み） |
| 操作 | `clear` / `press` / `release` / `isDown` | 同左 |
| 内部状態 | struct 1 個（`nkroState_`） | 同左（対応済み） |
| 送信経路の集約 | private 1 本（既存 `sendNkroReport()`） | `sendHeldNkroState()`（対応済み） |
| 状態参照 | `heldState()` | 同左（対応済み） |
| 未有効時 | 失敗 | 同左（`InvalidState`） |
| 送信 | `sendReport(report, timeoutMs)` | `sendReport(report)` |

`timeoutMs` の有無だけが差（BLE は notify なので送信 timeout の概念が異なる）。これにより ESP32KeyBridge の USB 出力 adapter と BLE 出力 adapter がほぼ同一コードになる。

**bitmap のサイズが Host 側と非対称な点は EspBle と同じ**。Host 側（EspUsbHost `EspUsbHostKeyboardState`、EspBle `EspBleHidKeyboardState`）は usage `0x00`〜`0xFF` の 32 byte、Device 側は Report Descriptor の宣言範囲に合わせて `0x00`〜`0xDF` の 28 byte。「Host で受けて Device で出す」経路では modifier は `modifiers` へ入り、それ以外の `0xE0` 超は表現できない（`press()` が false を返す）。この非対称は 3 ライブラリで揃っているので、README / DESIGN_NOTES では EspBle と同じ説明にする。

**EspUsbDevice 側だけが未実装の状態。** 対称性が崩れると、複数ライブラリを併用する利用者が毎回差分を意識することになる。

## 項目②: LED 状態の観測（listener 化は保留）

### 現状と要求元の状況

ESP32KeyBridge の出力 adapter は、bridge へ Lock 状態を返すためにコンストラクタで `keyboard_.onOutputReport()` を取る（`src/ESP32KeyBridgeEspUsbDevice.h`）。`onOutputReport()` は単一 slot の `std::function` なので、**adapter がフックを占有するとスケッチ側から LED 状態を観測できない**（外付け Caps Lock LED を光らせる、など）。

### 提案: listener 化ではなく getter 追加

当初は listener 化を検討したが、この用途には過剰である。LED は **event ではなく状態**であり、競合しているのは「1 回の通知を誰が消費するか」ではなく「最新値を誰が読めるか」でしかない。

```cpp
class EspUsbDeviceHidKeyboard {
public:
  // Latest LED output report from the host. Updated regardless of whether an
  // onOutputReport() callback is installed, so a sketch can read Lock state even
  // when an integration layer owns the callback slot.
  const EspUsbDeviceHidKeyboardOutputReport &leds() const;
};
```

現状 `EspUsbDeviceHidKeyboardOutputReport`（`EspUsbDevice.h`）は callback 引数としてしか存在せず、最新値を保持するメンバも getter も無い。`onHidSetReport()` で組み立てている report をメンバに保存し、callback の有無に関係なく更新してから callback を呼ぶだけで済む。

listener 基盤の新設に比べて桁違いに安く、かつ EspBle の HID Device 側 `onOutputReport()` が単一 slot である現状（= Device 側同士の一貫性）を崩さない。

### listener 化を保留する理由

| ライブラリ | listener API |
|---|---|
| EspUsbHost | 入力 6 種にあり（2.4.0）、device lifecycle / MIDI を追加予定 |
| EspBle | GATT client / HID Host event にあり、接続 lifecycle を追加予定 |
| EspUsbDevice | **なし** |

1. 出力 adapter は 1 デバイスに 1 つが普通で、EspUsbHost 側のように 4 つの adapter が同じフックを奪い合う状況になりにくい
2. EspBle の HID Device 側 `onOutputReport()` も単一 slot なので、Device 側同士では既に一貫している
3. 本ライブラリには listener 基盤自体が無く、EspUsbHost への追加より作業が大きい
4. 上記 getter で要求元の問題が解ける

EspUsbHost の仕様案で定めた切り分け（**観測系は listener 化可 / 応答系は単一 slot のまま**）に照らせば `onOutputReport` / `onProtocol` はどちらも観測系なので、将来 listener 化してよい対象ではある。やるなら EspBle の HID Device 側と揃えて同時に行う。ただし本件の動機からは外す。

## 対象外

`ready()` は既にあり、ESP32KeyBridge の出力 adapter が `OutputAdapter::connected()` の実装に使えている。EspBle 提案①（送信可否クエリ）に相当する穴は本ライブラリには無い。

## 検証

項目①について。

- `MaxBitmapUsage` 超えの usage を `press()` が拒否すること、modifier usage が `modifiers` へ振り分けられること、`isDown()` が両方を正しく読むこと（unit test で足りる）
- `enableNkro()` 未実行時に新 overload が失敗すること（unit test）
- `heldState()` が直前に送った状態を返すこと。`pressUsage()` / `releaseUsage()` / `releaseAll()` を挟んでも追従すること（unit test）
- `releaseUsage()` が modifier usage を落とすこと（上記 実装方針 7 の回帰。unit test）
- boot protocol 選択時に 6 キーの boot report へフォールバックすること。7 キー以上のときに usage 番号順の先頭 6 個が出ること（明文化した挙動の固定）
- 既存 boot report `sendReport()` の挙動が変わらないこと（回帰）
- NKRO 有効時、7 キー以上を含む report を 1 回送って Host 側が全キーを認識すること

**最後の項目（実機 peer test）は現時点で blocked。** `docs/DESIGN_NOTES.ja.md` の NKRO 節が既に「2 台 peer テストは sibling の `EspUsbHost` が NKRO bitmap をパースできるか次第」と記録しているとおりで、実際 NKRO の peer / loopback テストは 1 本も無い。本件の検証も同じ依存にぶつかる。

したがって段取りは次のとおり。

1. まず `onHIDInput` の生バイト（29 byte の modifier + bitmap）で 7 キー以上が乗っていることを確認する
2. 正式な peer test（Host 側で全キーが key event として認識される）は **EspUsbHost の NKRO bitmap パース対応後の follow-up** とし、TODO に残す

EspBle 側は Host / Device 双方が NKRO bitmap を扱えるため既に peer 検証済み（`tests/peer/hid_keyboard_nkro`）。USB 側だけがこの穴を持つ。

## 波及

- `src/EspUsbDevice.h` / `src/EspUsbDevice.cpp`
- `keywords.txt`、`CHANGELOG.md`
- `README.md` / `README.ja.md` の HID keyboard / NKRO 節
- `docs/DESIGN_NOTES.ja.md` の NKRO 節（未有効時に失敗させる理由、boot fold-down の明文化、重複抑制は呼び出し側の責務）
- ~~**先行**: EspBle 側への変更依頼~~ → **完了**（`docs/ESPBLE_NKRO_API_CHANGE_REQUEST.ja.md` の A〜E すべて対応済み。Host 側 `EspBleHidKeyboardState` と EspUsbHost 2.7.0 も同じ命名へ改名された）
- 増分 API のエラー化（範囲外 usage を黙って無視 → false）と `releaseUsage()` の modifier クリアは既存挙動の変更なので、CHANGELOG に breaking ではないが挙動変更として明記する
- 新 overload の追加により `sendReport({})` のようなブレース初期化呼び出しは overload 曖昧になる。repo 内の examples / tests に該当は無いが、利用者スケッチには有り得るので CHANGELOG に明記する
- 採用後、ESP32KeyBridge の `src/ESP32KeyBridgeEspUsbDevice.h` を NKRO 対応にし、`buildHidKeyboardRolloverReport()` の使い道を決める（bitmap 版のビルダーを追加するか、adapter 側で `KeySet` から直接 bitmap を組むか）。重複抑制も adapter 側に置く
