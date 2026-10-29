# v3開発の引き渡し

更新: 2026-10-29。worktree `/home/mt/dev/EspUsbDevice-v3`、branch `v3`。
最新製品は`80e8676`。remote wakeupの停止guardまで実装し、S3の新規境界試験と
S2/P4の修正後compile、最小構成の再測定が完了。**標準comboの全47件は実行中**。
全47件の成功をまだ確定していない。製品と対応probeを変更せず結果を待つ。
初回は19成功後にregistryの旧descriptor長161の固定期待で停止（実長175）。
試験0144bfbでDevice申告とHost実取得長の照合へ修正し、全47件を再実行中。
初回20実行・40 logs全停止/panicなしの記録は`remote-all-initial*`へ保存した。

## 方針と入口

Device/Function/config/beginの明示的な構成を維持し、使用する機能だけに費用を持たせる。
旧互換性より一貫した最終形を優先するが、利点のない改名や互換shimは作らない。
CH32RVはAPI調整の設計事例、OEPは利用者の一例であり、移植・特化の対象ではない。
確認不要で進められる単位を実装・実測し、区切りごとにlocal commitする。pushしない。

- [開始停止・移行](V3_MIGRATION.ja.md)、[API判断](V3_API_DECISIONS.ja.md)、[所有権](V3_ARCHITECTURE.ja.md)
- [独立試験](../validation/README.ja.md)、[全体計画](V3_DEVELOPMENT_PLAN.ja.md)
- [HID](V3_HID_API.ja.md)、[Audio](V3_AUDIO_API.ja.md)、[Remote wakeup](V3_REMOTE_WAKEUP.ja.md)
- [CH32RV比較](V3_CH32RV_API_ALIGNMENT.ja.md)、[DWC2の由来と保守](../third_party/tinyusb/PROVENANCE.ja.md)

## 現在の実装

Vendor/CDC/HID/MIDI/MSC/Audio/DFU/NCM/CCID/Video、descriptor registry、EP0/core、
runtime、controller/IRQ/cache、保守DWC2 forkまで本体内部で維持する。
旧TinyUSB API・vendor build snapshot/configやSDK同名headerへのfallbackはない。
MIT/ST BSDの出所表示は保持。構造検査だけでなく、compiler依存・nmと実機を合わせて確認する。

資源はinstance設定に応じて開始時確保し、end/rollbackで回収する。
通常転送中の反復heap取得を避け、未選択Functionのstateやkeymapを共通表で引き込まない。
`started`は資源所有、`ready`はHost構成、`suspended`はbus休止。DescriptorsOnlyはworker/PHYなし。
CDCはArduino Stream、VendorはPrint、MIDIはpacket API。worker設定も開始configへ移した。

## 検証済みの範囲

| 対象 | 証拠・結果 |
| --- | --- |
| 全体の直前基準052bcbb | 全44件3745.31秒成功、88 serial全STOPPED/panicなし。816開始＋14設定失敗回復、heap一致。`archive-all*` |
| examples | 052bcbbの新8例×S3/S2/P4＝24 compile。各ELF/FQBN/archive/29依存/native出所を監査。`archive-examples-*` |
| typed HID | System標準usage/array解放、Gamepad角度/中央Null、複合global分離。44照合/52 input、4組のdescriptor意味検査、28不正要求と無信号。新規1件と関連6項目成功。`hid-globals-*` |
| 失敗注入の更新 | 47構成で実測した817取得点全て失敗/回復、14設定失敗、heap346768一致。固定取得数の期待を撤去。`hid-globals-faults*` |
| RAM/FAT helper | raw/FAT各4実往復、8開始/guard、RAMのみ。`msc-helpers*` |
| Remote wakeup 80e8676 | 新規1件45.25秒成功、2 logs全停止/panicなし。3実wake/7無信号/10往復、queued reset/重複拒否。停止中17回＋停止後32回の要求拒否/許可false。`remote-wakeup-stop-after*` |
| S2/P4 80e8676 | remote probeのcompile/ELF機械種別/archive/29依存/native pulse・IO・delay出所と未選択class除外成功。`remote-final-cross-audit.json` |
| 最小構成80e8676 | includeのみ259465 Flash/21584 globals、追加0/0。Device宣言のみ261957/21840、object196 byte。wake/worker/PHY開始symbolなし。`remote-final-unused-audit.json` |

成果物は`validation/.pytest-results/`（git対象外）。最小Device構成は旧archive基準より
Flash+44/global+16。objectサイズ増ではなく、共通生存確認lockのリンク等による差分。
HID修正中にdescriptor長/取得点の古い固定期待が2件失敗した事実と、修正後の成功を区別する。
関連6項目は複数実行で確認したもので、一括6件成功のJUnitではない。

## 現在実行中の確認

製品/probeを`80e8676`に固定し、47件を標準comboで実行している。
JUnitは`validation/.pytest-results/remote-all.xml`、進捗は`/tmp/espusb-v3-remote-all.txt`。
終了後に47件・skip/error/failure/unfinished、94 serialの停止/panic、実測取得点の総数・回収、
class/control/ISO/HID/helper/wakeのwire指標とnative依存を監査する。
監査scriptは`/tmp/espusb-v3-remote-all-audit.py`。実行中に古いXMLやELFで成功を確定しない。

## 試験の運用

`validation/`だけを開発試験の入口とする。旧tests/examplesを合否基準やhelperとして使わない。
plugin1.10.0（local `698bb29`）、Core3.3.12、EspUsbHost2.10.1（必要probeのみ）。
標準combo `s3: primary=s3_host,device=s3_device`、`--noconftest --maxfail=1`。
独自conftest/pluginへhookを移さない。不足機能はplugin側の拡張相談を優先する。

```sh
cd /home/mt/dev/EspUsbDevice-v3/validation
uv sync --locked
uv run --locked --env-file .env pytest --combo all --collect-only -q
uv run --locked --env-file .env pytest --combo all -q -rs --junitxml=.pytest-results/v3-all.xml
```

実機pytestは一つずつ。対応するbuild/test中は製品とprobeを変更しない。
別probe/exampleのcompileも出力先を分離する。共有`/tmp/pytest-embedded`の最新directoryを
根拠にせず、JUnit timestampから当該serial原本を特定する。

## 未完了と次の判断

まず固定sourceの全47件を終えて監査・記録する。追加変更や全件再実行は結果に応じて判断する。
S2/P4実機（HS/cache含む）、S31 port、PC OS結線、SD/OTA実媒体、長期連続、
shared toolkitの配布範囲・release判断は残る。S3 peer成功をこれらの代替にしない。
remote wakeupは実装済みだが、SoC sleep/PC電源管理/電気適合性を保証した結果ではない。

[過去の実装・実測記録](V3_PROGRESS_HISTORY.ja.md)に整理前の引き渡し全文を保存した。
途中段階の「未実装」「次」を現在の未完と混同しない。詳細は各APIガイドとvalidation READMEへ置く。
