# EspBle からの返答への応答（LED 状態 getter）

対象: `EspBle/docs/REPLY_ESPBLE_LED_STATE.ja.md`
関連: `docs/ESPBLE_LED_STATE_REQUEST.ja.md`、`docs/KEYBRIDGE_ADAPTER_PROPOSAL.ja.md` 項目②

## 結論

**値返しの判断は EspBle 側が正しく、EspUsbDevice も値返しへ変更した。** 「USB 側で参照が成立するのは
`onHidSetReport()` の実行 context がこの制約を持たないため」という理解は**誤り**で、EspUsbDevice にも
まったく同じ task 境界がある。返答をそのまま受け入れて据え置いていたら、こちらにデータ競合が残っていた。

また、指摘のうち「切断時に `heldState()` もクリアしないと stuck key になる」は EspUsbDevice にも
そのまま当てはまる**未修正の不具合**だった。こちらも直した。

## 1. EspUsbDevice にも task 境界がある（返答の前提の訂正）

v2 の EspUsbDevice は Arduino Core の USB stack を使わず、**自前の FreeRTOS task で `tud_task()` を回す**
（`src/internal/EspUsbTinyUsbRuntime.cpp:82`、task 名 `espusb-device`）。したがって

- `tud_hid_set_report_cb` → `onHidSetReport()` は **`espusb-device` task** で走る
- `ledState()` を呼ぶのはスケッチの loop task

で、EspBle の「stack task が書き、loop task が読む」とまったく同じ構図になる。旧実装は
`EspUsbDeviceHidKeyboardOutputReport`（`uint8_t` + `bool` 5 個）をメンバに丸ごと持ち、参照を返していたので、
**フィールドが途中状態で混ざった組み合わせ（`leds` は新しいが `capsLock` は古い）を読める**状態だった。

修正内容:

```cpp
// 保持は raw byte 1 個だけ。1 回の store / load は途中状態を観測できない
mutable std::atomic<uint8_t> ledsRaw_{0};

EspUsbDeviceHidKeyboardOutputReport ledState() const;  // 値返し
```

`ledState()` は `ledsRaw_` の 1 回の load から report を組み立てる。mutex は不要で、
**フィールド間の不整合が原理的に起きない**。EspBle の「ロック内でコピーして値を返す」と等価な保証。

`onOutputReport()` の callback へ渡す report も同じ builder（`makeKeyboardOutputReport()`）から作るので、
callback と getter でビットの意味が食い違うことはない。

## 2. `heldState()` は両ライブラリとも参照のままでよい（ただし条件がある）

EspBle の整理（`heldState()` は送信経路＝呼び出し側 task だけが書くので参照可）に同意する。EspUsbDevice も
参照のままにした。

ただし **3 の修正を素直に書くと、この前提が壊れる。** 切断時のクリアを USB task で実行すると
`nkroState_` の書き手が 2 つになり、`heldState()` が返す参照が競合する。そこで

- USB task（`onBusAttached()` / `onBusDetached()`）は **atomic flag を立てるだけ**
- 実際の `nkroState_.clear()` は、スケッチ側 task の入口（`sendReport` / `pressUsage` / `releaseUsage` /
  `releaseAll` / `heldState`）で `applyPendingBusChange()` が行う

という遅延クリアにした。これで `nkroState_` の書き手はスケッチ task 1 つのままで、参照返しが維持できる。
LED 側は atomic byte 1 個なので USB task から直接消してよい。

**EspBle 側も同じ確認をしてほしい。** 返答には「同じ場所で `heldState()` もクリアするよう直した」とあるが、
その「同じ場所」が GATT / stack task なら、`heldState()` が `const &` を返している以上、
こちらが踏んだのと同じ競合になる。mutex 内でクリアしていて、かつ `heldState()` も mutex 内でコピーを返すなら
問題ないが、`heldState()` が参照返しのままだと**参照を受け取った後の読み出しがロック外**になる。
確認事項は 1 点だけ:

> 切断時の `heldState()` クリアはどの task で走り、`heldState()` の呼び出し側は
> ロック外でその実体を読むことにならないか

## 3. 切断クリアの指摘は EspUsbDevice にも当てはまった（修正済み）

指摘のとおり、状態ベースの adapter が `heldState()` と比較して重複送信を抑制する使い方をするので、
再接続をまたいで保持状態が残ると「前回と同じだから送らない」で **stuck key** になる。USB でも
抜線・再挿入でオブジェクトは生き残るため同じことが起きる。`begin()` でのクリアだけでは不足だった。

対応:

- `tud_mount_cb` / `tud_umount_cb` を実装し、`EspUsbDeviceClass::onBusAttached()` / `onBusDetached()`
  （既定 no-op）へ配送。keyboard が LED 状態と NKRO 保持状態（と 6KRO の `report_`）を捨てる
- **hook が 2 つ必要だった。** `tud_umount_cb` は `SET_CONFIGURATION 0` / deinit /（VBUS sensing のある
  ボードでのみ）抜線で呼ばれ、**素の bus reset では呼ばれない**。VBUS sensing の無い ESP32 ボードで
  再挿入すると umount を経ずに再 enumeration へ進むため、常に発火する `tud_mount_cb` 側でもクリアする。
  mount 時点で Host は「何も押されていない・LED 未設定」と認識しているので、どちらで消しても正しい

BLE には対応する話は無い（切断は必ず観測できる）ので、これは USB 固有の追加分。

## 4. 受け入れたその他の点

| 返答の項目 | EspUsbDevice 側 |
|---|---|
| 値返し | 採用（上記 1） |
| `ledState()` の命名 | 一致 |
| listener 化しない | 一致。`onOutputReport` / `onProtocol` は単一 slot のまま |
| callback 未設定でも更新 | 一致（保存は dispatch より前） |
| queue 溢れ対策で保存位置をさらに手前へ | **USB 固有の事情で不要。** EspUsbDevice は output report を queue せず `onHidSetReport()` で同期処理するので、drain されない queue が無い |
| `ledState()` が `onOutputReport()` より先行しうる | EspUsbDevice では起きない（同じ callback 内で保存と dispatch を連続実行するため）。この差は SPEC に書き分けるだけでよく、揃える必要は無いと考える |
| 複数 Host（`connectionId` 付き・最後に書いた Host の値） | USB は Host 1 台なので該当なし |
| CHANGELOG を触らない判断 | 妥当。EspUsbDevice はリリース済みなので通常どおり記載した |

## 5. 未決（両者の既存差）

`EspBleHidKeyboardOutputReport::numLock()`（メソッド）と `EspUsbDeviceHidKeyboardOutputReport::numLock`
（bool メンバ）の差は今回そのまま。**揃えるなら EspBle 1.0.0 前が最後の機会**なので、提案があれば受ける。

こちらの立場: `ledState().capsLock` のように**メンバのほうが読みやすい**と考えるが、EspBle 側は
raw byte から都度計算する形（メソッド）が状態を二重に持たない利点がある。どちらでも運用できるので、
EspBle 側の判断に合わせる用意がある。決めるなら早いほうがよい。
