# v3開発の引き渡し

更新: 2026-10-29。worktree `/home/mt/dev/EspUsbDevice-v3`、branch `v3`。
最新製品は`80e8676`、registry試験修正は`0144bfb`。
**標準combo allの全47件が1917.29秒で成功**。skip/error/failure/unfinishedは0、
94 serial全STOPPED/panicなし。実測817開始＋14設定失敗回復、heap一致も監査した。
初回はregistryの旧descriptor長161の固定期待で停止（実長175、19成功・1失敗）。
試験をDevice申告とHost実取得長の照合へ直し、製品を変えずに全47件を再実行して成功した。
初回の40 logs全停止/panicなしの記録は`remote-all-initial*`へ保存した。

P4追加: probe f5c69dd、6件67.84秒成功。両controllerのFS loopback、18開始/24停止、
Vendor/CDC/HIDと65537 byteまでのDMAを確認。[P4記録](../validation/p4/README.ja.md)。
公開Host同期INのcache欠落を再現し、Host隔離branch 7f25896で比較成功。
通常P4試験は公開Host2.10.1のqueue経路で成功し、未公開版には依存しない。

P4次段: 0bf46ddでMSC/MIDI追加4件31.85秒成功、4 logs正常停止/panicなし。
両実機はv1.3。P4Xはpostv3 compile/ELF/29依存まで確認し、HW検証は未実施。
[機種差・必要環境と次の試験](V3_P4_VARIANTS_AND_BENCH.ja.md)。PSRAMは53bcfebで2件成功、32 MiBと外部保存先/ringの実通信を確認。
Audio e577698のUAC1/UAC2 duplex4件も33.95秒成功。8開始、PCM両方向/feedback/formatを確認。
PSRAM/Audioのpostv3 compile・29依存/native出所も監査済み。最新収集63件。
次はDFU/CCID/Video/NCM。HS結線用のboard型番/2台目native接続先は質問済み、未回答。

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
| 最新全体80e8676/0144bfb | **47 passed、1917.29秒、skip/error/failure/unfinished 0**。94 logs全停止/panicなし。817開始＋14設定失敗回復、heap346768一致。typed HID/helper/remote含む。`remote-all*` |
| 全体の直前基準052bcbb | 全44件3745.31秒成功、88 serial全STOPPED/panicなし。816開始＋14設定失敗回復、heap一致。`archive-all*` |
| examples 80e8676 | 新8例×S3/S2/P4＝24 compile、全成功。各ELF/FQBN/機械種別/archive/29依存/native出所・未選択class除外を監査。`final-examples-audit.json`。052bcbbの旧結果は`archive-examples-*` |
| typed HID | System標準usage/array解放、Gamepad角度/中央Null、複合global分離。44照合/52 input、4組のdescriptor意味検査、28不正要求と無信号。新規1件と関連6項目成功。`hid-globals-*` |
| 失敗注入の更新 | 47構成で実測した817取得点全て失敗/回復、14設定失敗、heap346768一致。固定取得数の期待を撤去。`hid-globals-faults*` |
| RAM/FAT helper | raw/FAT各4実往復、8開始/guard、RAMのみ。`msc-helpers*` |
| Remote wakeup 80e8676 | 新規1件45.25秒成功、2 logs全停止/panicなし。3実wake/7無信号/10往復、queued reset/重複拒否。停止中17回＋停止後32回の要求拒否/許可false。`remote-wakeup-stop-after*` |
| P4実機 | FS/HS controllerを各FS linkで確認。Vendor132 copy往復/1536 direct完了、48 RX＋48 TX返却、CDC24往復/HID18 press/release。6 logs正常停止。`p4-final-audit.json` |
| S2/P4 compile 80e8676 | remote probeのcompile/ELF機械種別/archive/29依存/native pulse・IO・delay出所と未選択class除外成功。`remote-final-cross-audit.json` |
| 最小構成80e8676 | includeのみ259465 Flash/21584 globals、追加0/0。Device宣言のみ261957/21840、object196 byte。wake/worker/PHY開始symbolなし。`remote-final-unused-audit.json` |

成果物は`validation/.pytest-results/`（git対象外）。最小Device構成は旧archive基準より
Flash+44/global+16。objectサイズ増ではなく、共通生存確認lockのリンク等による差分。
HID修正中にdescriptor長/取得点の古い固定期待が2件失敗した事実と、修正後の成功を区別する。
関連6項目は複数実行で確認したもので、一括6件成功のJUnitではない。

## 最新全体監査

製品/probeを固定し、標準combo allで47件全てを同一実行で確認した。
JUnitは`validation/.pytest-results/remote-all.xml`、集計`remote-all-audit.txt`、
原本`/tmp/pytest-embedded/2026-10-29_11-06-05-103463/`。
同prefixのserial/transcriptと、監査script`remote-all-audit.py`も成果物へ保存した。

Audio 32 ISO交換/28旧buffer poison/36 guard、4 Audio Fnの10順次交換、
CDC＋Audio/MSCの同時往復、NCM/CCID/VideoのHALT回復、reset/cancel/休止を確認。
Vendor 1000 frame/713000 byte×2・39再接続・80 reset、heap356392一致。
registryは申告/EP0取得長175一致、7 report IDを確認した。
remoteは実wake3回/無信号7回/10往復、停止中17回・停止後32回の拒否/照会false。
S3 VendorとS2/P4 Vendor＋remoteの29依存/archive/native出所・未選択class除外も監査した。

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

## 今回の依頼との照合

S3–S3を初期環境として、確認不要で進められる実装と検証を行った。
公開releaseや未入手機材の実機認定を完了したという意味ではない。

| 要求 | 現在の証拠 |
| --- | --- |
| 基本方針を維持し、CH32RV移植/OEP特化にしない | Device/Function/config/beginの公開API、新8例、API_DECISIONS/CH32RV比較。CH32RV/OEP側は変更しない |
| build時設定をAPI/構成へ移す | Device/各Functionのconfigure、旧flagの明示拒否、開始/終了・容量・複数instance・失敗回復を全47件で確認 |
| TinyUSB依存を最後まで除く | 10 class/core/runtime/保守DWC2、構造検査・29 source依存・native symbol出所・license記録。SDK同名headerへのfallbackなし |
| 使う機能だけの費用 | include追加0/0、Deviceのみの差分説明、24例とprobeの未選択state除外、817取得失敗回復とheap一致 |
| 自然な基本利用と、破壊変更の利点/不利益 | Vendor自動送出、CDC Stream、copy/borrowed所有権、HID usage、Audio format等の移行表/各API判断。現在版8例×3機種compile成功 |
| 旧test/sampleと独立し標準comboを使う | validationの新probe、plugin1.10.0、noconftest。combo allで47実行/47成功/未完0、94 logs正常停止 |
| S2/P4/S31への拡張余地 | port能力表とcontroller境界、S2/P4の29 source・8例compile。S31は新SDK/port対象と明示。五台benchの逐次役割案を全体計画へ記録 |
| mainとの整合・適度なcommit | mainの3fed50dを含む独立v3 worktree。各実装・試験・監査結果をlocal commitし、pushしない |

文書の相対link検査は現行README/V3 guides/examples/validationで不良0。
リポジトリ全体のcheckは旧版履歴・legacy移動に伴う399件を報告して失敗する。
旧資産をv3合否の制約にせず、原本は`validation/.pytest-results/doc-links-final.txt`へ保存した。

## 未完了と次の判断

固定sourceの全47件と関連監査は完了。新しい変更や未解決の懸念がなければ同じ試験を繰り返さない。
P4の未確認class・480 Mbps HS link、S2実機、S31 port、PC OS結線、SD/OTA実媒体、長期連続、
shared toolkitの配布範囲・release判断は残る。S3 peer成功をこれらの代替にしない。
remote wakeupは実装済みだが、SoC sleep/PC電源管理/電気適合性を保証した結果ではない。

[過去の実装・実測記録](V3_PROGRESS_HISTORY.ja.md)に整理前の引き渡し全文を保存した。
途中段階の「未実装」「次」を現在の未完と混同しない。詳細は各APIガイドとvalidation READMEへ置く。
