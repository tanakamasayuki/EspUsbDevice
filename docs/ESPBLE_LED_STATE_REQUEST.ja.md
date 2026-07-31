# EspBle HID Device: LED 状態 getter の追加依頼（1.0.0 リリース前）

依頼元: EspUsbDevice / ESP32KeyBridge
関連: EspUsbDevice `docs/KEYBRIDGE_ADAPTER_PROPOSAL.ja.md` 項目②、`EspBle/docs/DECISIONS.ja.md` 19-22（NKRO 側の対称化）

## 要旨

EspUsbDevice に `EspUsbDeviceHidKeyboard::ledState()` を追加した。**EspBle の HID Device 側にも同じ穴があるので、同形の getter を足してほしい。**

追加のみで破壊的変更を含まないが、**API 対称性の話なので 1.0.0 に間に合わせたい**（後から足すと「EspUsbDevice にはあるが EspBle には無い」期間が公開版に残る）。

## 問題

`EspBleHidKeyboard::onOutputReport()`（`src/EspBle.h:1222`）は単一 slot の `std::function` で、最新の LED 状態を保持するメンバも getter も無い。

ESP32KeyBridge の出力 adapter は bridge へ Lock 状態を返すために、この単一 slot をコンストラクタで取る。その結果、**スケッチ側から Lock 状態を読む手段が無くなる**（外付け Caps Lock LED を光らせる、など）。EspUsbDevice 側もまったく同じ形で同じ問題を持っていた。

## 依頼内容

```cpp
class EspBleHidKeyboard {
public:
  // Latest LED output report from the host. Updated whether or not an
  // onOutputReport() callback is installed.
  const EspBleHidKeyboardOutputReport &ledState() const;
};
```

実装は次の 3 点だけ。EspUsbDevice 側の実装（`src/EspUsbDevice.cpp` の `EspUsbDeviceHidKeyboard::onHidSetReport()` / `ledState()` / `begin()`）と同じ形にしてある。

1. **最新 report をメンバに保持する。** `EspBleHidKeyboardOutputReport ledState_;`
2. **callback の有無に関係なく更新する。** output report を組み立てた直後に保存し、callback dispatch はその後。EspUsbDevice 側は「callback 未設定なら早期 return」する旧構造だったため、保存位置を dispatch より前に動かす必要があった。EspBle の `dispatchPendingOutputReports()` 経路でも同様に、**dispatch 可否の判定より前**に保存すること
3. **接続開始時（または `configure()` / `begin()` 相当のタイミング）で初期化する。** 前の接続で受け取った LED を現在値として読ませない。EspBle は接続単位なので、`connectionId` を持つ struct をどう扱うか（切断時にクリアするか、最後の値を残すか）は EspBle 側の判断でよい。EspUsbDevice は `begin()` でクリアしている

## listener 化はしない（両ライブラリ共通の判断）

LED は event ではなく**状態**で、競合しているのは「1 回の通知を誰が消費するか」ではなく「最新値を誰が読めるか」でしかない。getter なら listener 基盤を増やさずに解決し、HID Device 側の `onOutputReport()` が単一 slot である現状（EspUsbDevice / EspBle で一貫）も崩さない。

`onOutputReport` / `onProtocolMode` の listener 化自体は将来の選択肢として残すが、この依頼には含めない。やるなら EspUsbDevice 側と同時。

## 命名について

`leds()` ではなく **`ledState()`** を使ってほしい。返す struct のメンバが `leds`（raw byte）なので、`leds()` だと `keyboard.leds().leds` になる。`ledState().capsLock` / `ledState().leds` なら両方素直に読める。

なお EspBle の `EspBleHidKeyboardOutputReport` は `numLock()` などが**メソッド**、EspUsbDevice の `EspUsbDeviceHidKeyboardOutputReport` は**bool メンバ**という既存の差がある（`ledState().capsLock()` vs `ledState().capsLock`）。これは既存の公開形なので今回は揃えない。揃えるなら別途、リリース前に判断が必要。

## 波及（EspBle 側）

- `src/EspBle.h` / `src/EspBle.cpp`
- `keywords.txt`、`CHANGELOG.md`
- `docs/HID_DEVICE_SPEC.ja.md`（keyboard 節）、`docs/DECISIONS.ja.md`（listener 化しない理由を 1 項目として記録）
- `tests/peer/hid_keyboard_device`（LED Output を既に検証しているのでそこへ）。EspUsbDevice 側は
  `tests/peer/hid_keyboard` に「callback あり → callback と getter が一致」「callback を外す → それでも getter が Host に追従」の 2 段で追加した。同じ形が有効
