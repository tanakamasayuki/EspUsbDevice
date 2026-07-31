# EspBle NKRO 状態送信 API 変更依頼（1.0.0 リリース前）— **対応済み・記録として保存**

依頼元: EspUsbDevice / ESP32KeyBridge
関連: EspUsbDevice `docs/KEYBRIDGE_ADAPTER_PROPOSAL.ja.md` 項目①

## 結果（2026-07-31 時点）

**A〜E すべて EspBle 側で対応済み。EspUsbDevice 側の実装ブロッカーは解消した。**

| 項目 | 結果 |
|---|---|
| A. `keys[]` → `bitmap[]` | ✅ 採用。**Host 側 `EspBleHidKeyboardState` にも同じ問題があり、そちらも破壊的変更で `bitmap` / `changedBitmap` へ改名**（依頼範囲より広い対応） |
| B. 内部状態を struct 1 個に | ✅ `nkroState_` に統一。全 NKRO 送信経路を private `sendHeldNkroState()` に集約 |
| C. `heldState()` 追加 | ✅ 追加。Boot Protocol Mode 中は「要求した状態」であって電波上のバイト列ではない旨も明記された |
| D. 失敗設計の理由 | ✅ `docs/DECISIONS.ja.md` 22 / `HID_DEVICE_SPEC.ja.md` に依頼と同じ「責任の所在」基準で記録 |
| E. 重複抑制の責務 | ✅ 同 21。抑制しない方針と `heldState()` を比較対象にする旨を記録 |

派生した確定事項が 2 つある。EspUsbDevice 側もこれに従う。

- **命名規則の一般化**（`docs/DECISIONS.ja.md` 19）: **bitmap を持つメンバは `bitmap`、usage の配列を持つメンバは `keys`**。姉妹 3 ライブラリ共通の規則になった。`EspUsbHost` も `EspUsbHostKeyboardState` を `bitmap` / `changedBitmap` へ改名して 2.7.0 でリリース済み（commit `5442c95`）。EspUsbDevice で bitmap を持つ公開メンバはこの新 struct が最初なので、`bitmap` で作れば追従は不要（既存の boot report `keys[6]` は usage 配列なので正しい）
- **`press()` / `release()` の戻り値をエラー通知の源にする**（同 20）: EspBle は `pressUsage()` / `releaseUsage()` が表現できない usage に返していたエラーを、struct の `press()` / `release()` の戻り値から再構成する形にした。EspUsbDevice も増分 API 側を `nkroState_` 経由に書き換える際、同じ形にする

以下は依頼時点の原文（判断の経緯として保存）。

## 要旨

EspBle の NKRO 全状態送信 API（`EspBleHidKeyboardNkroReport` + `sendReport()` overload、`src/EspBle.h:650-699` / `src/EspBle.cpp:7086-7102`）は既に実装・peer テスト済みで、設計として妥当である。**この依頼は動作の変更ではなく、公開名と内部構造の整理**である。

EspUsbDevice 側にこれと対称な API を追加しようとしており（現状は 6 キー版 `sendReport()` しか無く、ESP32KeyBridge 経由では USB で NKRO を出せない）、両ライブラリで**同一形状**にする方針を取っている。EspBle が先に実装済みなので、EspBle を基準に合わせるのが自然だが、下記 A は EspBle 側を直さないと API が名前として誤解を招く形で固定される。

**A は破壊的変更で、1.0.0 リリース後には直せない。** EspUsbDevice 側の実装は A の可否が決まるまで着手しない（命名が未確定のまま実装すると両方を再度直すことになる）。

## A. `keys[]` → `bitmap[]` へ改名（破壊的・要リリース前）

対象: `src/EspBle.h:656` および `press()` / `release()` / `isDown()` / `clear()` 内の参照、`src/EspBle.cpp:7098`（`memcpy(nkroBitmap_, report.keys, ...)`）、example / peer test / `docs/HID_DEVICE_SPEC.ja.md:38`。

```cpp
struct EspBleHidKeyboardNkroReport
{
  uint8_t modifiers = 0;
  uint8_t bitmap[BitmapSize] = {};   // ← 現状 keys[BitmapSize]
};
```

理由: 6KRO 側の `keys[6]` は **usage の配列**（`keys[0] = 0x04` は「A が押されている」）だが、NKRO 側の `keys[28]` は **bitmap**（`keys[0] = 0x10` は「usage 0x04 が押されている」）で、同名なのに添字の意味が違う。両者は同じクラスの `sendReport()` overload として並ぶため、取り違えが起きやすい。

実際 `src/EspBle.cpp:7409-7411`（`pressUsage()` の 6KRO 経路）には

```cpp
report.keys[0] = usage;
```

があり、これは 6KRO 版 report のためのコードだが、NKRO 版 struct に対して同じ行を書くと**コンパイルは通って挙動だけ壊れる**（usage 0x04 を書くと「usage 3 と 5 が押された」ことになる）。名前が違えばこの間違いは書けなくなる。

`bitmap` なら型と意味が一致し、`bitmap[usage >> 3] & (1u << (usage & 7))` という利用側のコードも自明に読める。

## B. 内部状態を struct 1 個にする（非破壊・任意だが強く推奨）

対象: `src/EspBle.h:1262-1263`、`src/EspBle.cpp:7072-7078` / `7086-7102` / `7396-7406` / `7419-7428` / `7494-7495` / `7573-7574` / `6627-6628`。

```cpp
// src/EspBle.h
EspBleHidKeyboardNkroReport nkroState_;   // ← nkroModifiers_ + nkroBitmap_[28] を置換
```

現状、同じビット演算が最低 4 箇所に重複している。

| 箇所 | 内容 |
|---|---|
| `src/EspBle.h:666-689` | struct の `press()` / `release()`（modifier 振り分け含む） |
| `src/EspBle.cpp:7396-7398` | `pressUsage()` の同じ演算 |
| `src/EspBle.cpp:7419-7421` | `releaseUsage()` の同じ演算 |
| `src/EspBle.cpp:7076` | 6KRO overload が bitmap を組む際の同じ演算 |

`nkroState_` に統一すれば `nkroState_.press(usage)` / `.release(usage)` の呼び出しになり、modifier 振り分け（`0xE0`〜`0xE7`）の正解も 1 箇所になる。29-byte の組み立ても

```cpp
uint8_t value[29] = {nkroState_.modifiers};
memcpy(value + 1, nkroState_.bitmap, sizeof(nkroState_.bitmap));
```

と、6 箇所で同型のまま書ける（`sizeof` が struct 由来になる分、`BitmapSize` との不一致も起きない）。挙動は変わらないので、リリース後でも入れられるが、C と同時にやるのが自然。

## C. `heldState()` の追加（追加のみ・非破壊）

```cpp
class EspBleHidKeyboard {
public:
  // The state the host was last told about (NKRO only).
  const EspBleHidKeyboardNkroReport &heldState() const;
};
```

理由: ESP32KeyBridge の出力 adapter は毎周期にキー集合全体を受け取る設計で、

- ライブラリ内部状態と adapter の期待状態がずれたときの復旧手段が現状は `releaseAll()` の全消ししかない
- 「前回と同じ状態なら送らない」重複抑制を adapter 側でやる場合、adapter が shadow copy を持たされる（ずれの温床が増える）

`heldState()` があればどちらも解決する。B を先に入れていれば実装はメンバ参照を返すだけ。

## D. 未有効時に失敗させる設計は変更不要（判断の記録のみ）

`src/EspBle.cpp:7088-7093` が `enableNkro()` 未実行時に `InvalidState` で失敗させるのは**そのままでよい**。EspUsbDevice 側もこれに合わせる。

ただし、Host が Boot Protocol を選んだときは bitmap を 6 キーへ畳んで送る（＝失敗させない）ので、**同じ「NKRO で送れない状況」に対して挙動が 2 通りある**。判断基準を `docs/HID_DEVICE_SPEC.ja.md` に一文で残してほしい。

- Boot Protocol は **Host 主導の実行時条件**。スケッチに責任は無いので、送れる形に畳んで送る
- `enableNkro()` 忘れは **設定ミス**。畳んで成功させると 7 キー目以降が恒久的に無言で消えるため、即座に気付ける失敗にする

呼び出し側は `nkroEnabled()` で事前判定できるので、失敗設計でも「送れるか」のクエリは不足しない。

## E. 重複送信の責務を明記（docs のみ）

状態ベースの呼び出し側は毎周期 `sendReport(nkroReport)` を呼ぶ。**ライブラリ側で「前回と同じなら送らない」抑制を入れない**方針を明記してほしい（EspUsbDevice 側も同じにする）。BLE では connection interval に律速されるため抑制の誘惑が強いが、`releaseAll()` や Protocol Mode 切替を挟んだ際の再同期が読めなくなる。抑制は呼び出し側の責務とし、C の `heldState()` で比較できるようにする、が確定形。

## 優先度と期限

| 項目 | 破壊的 | リリース前必須 |
|---|---|---|
| A. `keys[]` → `bitmap[]` | ✅ | **✅ 必須** |
| B. 内部状態を struct 1 個に | — | 望ましい |
| C. `heldState()` 追加 | — | 望ましい（追加なので後でも可） |
| D. 失敗設計の理由を明記 | — | docs のみ |
| E. 重複抑制の責務を明記 | — | docs のみ |

A のみ 1.0.0 リリース前に確定が必要。**A が却下される場合は EspUsbDevice 側も `keys[]` で揃えるので、可否だけ早めに返してほしい**（対称性を崩す選択肢は取らない）。

## 波及（EspBle 側）

- `src/EspBle.h` / `src/EspBle.cpp`
- `keywords.txt`、`CHANGELOG.md`（未リリースなので breaking として書く必要はない）
- `docs/HID_DEVICE_SPEC.ja.md`（NKRO 節。A の名前、C の追加、D / E の方針）
- `tests/peer/hid_keyboard_nkro`（`keys` 参照の改名、`heldState()` の検証を追加）
- NKRO を使う example
