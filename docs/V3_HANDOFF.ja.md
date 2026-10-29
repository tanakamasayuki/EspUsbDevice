# v3開発の引き渡し

更新: 2026-10-29。作業場所`/home/mt/dev/EspUsbDevice-v3`、branch `v3`。
最新製品は疎なIN endpointに対するDWC2 TX FIFO割当てを修正。Audio外部ring長指定はe329ef6。metadataは3.0.0、未release。
mainの文書commit `3fed50d`を取り込み済み。まとまりごとのローカルcommit、pushなし。

## 方針と入口

Device/Function/config/beginの明示的な構成を維持し、使用する機能だけに費用を持たせる。
旧互換性より一貫した最終形を優先するが、利点のない改名や互換shimは作らない。
CH32RVはAPI調整の設計事例、OEPは利用者の一例であり、移植・特化の対象ではない。

- [開始停止・移行](V3_MIGRATION.ja.md)、[API判断](V3_API_DECISIONS.ja.md)
- [独立試験](../validation/README.ja.md)、[所有権](V3_ARCHITECTURE.ja.md)
- [全体計画](V3_DEVELOPMENT_PLAN.ja.md)、[CH32RV比較](V3_CH32RV_API_ALIGNMENT.ja.md)
- [DWC2の由来と保守](../third_party/tinyusb/PROVENANCE.ja.md)

[過去の実装・実測記録](V3_PROGRESS_HISTORY.ja.md)は履歴資料。
今回整理前の引き渡し全文もそこへ保存した。古い「未完」を現状と混同しない。
詳細な結果はvalidation READMEと各APIガイドへ置き、この文書に重複して積み増さない。

## 現在の実装

Vendor/CDC/HID/MIDI/MSC/Audio/DFU/NCM/CCID/Video、descriptor registry、EP0/core、
runtime、controller/IRQ/cache、保守DWC2 forkまで本体内部で維持する。
旧TinyUSB API・vendor build snapshot/configやSDK同名headerへのfallbackはない。
MIT/ST BSDの出所表示は保持。構造検査だけでなく、compiler依存・nmと実機を合わせて確認する。

- 資源はinstance設定に応じて開始時確保し、end/rollbackで回収。通常転送中の反復heap取得は避ける。
- `started`は資源所有、`ready`はHost構成、`suspended`はbus休止。DescriptorsOnlyはworker/PHYなし。
- workerのstack/priority/coreとevent容量は開始config。空taskやstartTinyUsb互換入口は撤去。
- CDCはArduino Stream、VendorはPrint。MIDIはpacket APIでPrint/Streamではない。
- borrowed bulkの総長はmin(UINT32_MAX,PTRDIFF_MAX)。上限queryをそのままmallocせず、アプリの予算を決める。
- [Audio](V3_AUDIO_API.ja.md)はformat保存を実数化し、停止後のclearFormats、共有clockの複数rate、
  PCM形状のalternate、currentFormat snapshotに対応。最大frame/packetを開始時に予約し、fence後に再activate。
  外部ringはpointer＋提供byte長を渡し、最大frame幅に対する不足をbeginで拒否する。
- ISO OUTの間欠送信で最初のpacketだけ受信する問題を実測し、4480513でincomplete OUTをDMA停止後に返却・再arm。
- 未選択Functionのstateやkeymapを共通表で引き込まない。Vendor単独ELFとS2/P4 Vendor＋CCIDで確認する。

## 最新の検証範囲

| 単位 | 結果・記録 |
| --- | --- |
| Audio＋CDC/FIFO修正後 | 4構成のfocused 1件36.60秒成功、2 logs STOPPED/panicなし。S2/P4 compile成功。全40件はこれから実行 |
| CDC＋MSC同時通信 | 新規1件59.01秒＋既存2件81.69秒、6 logs STOPPED/panicなし。製品e329ef6の変更なし。全体収集39件 |
| 外部ring長指定後の関連6件 | **6 passed、369.22秒**。12 logs STOPPED/panicなし、816開始＋14設定失敗回復/heap一致。`.pytest-results/audio-buffer-extents*` |
| 製品033f2d8の全38件 | **38 passed、2544.60秒、skip/error/failure/unfinishedなし**。76 logs STOPPED/panicなし。`.pytest-results/audio-alternates-all*` |
| 外部ring/能力表の追加試験 | 2件124.49秒＋1件31.33秒＋共有probe 3件88.15秒。全て成功、計12 logs STOPPED/panicなし。製品変更なし |
| PCM alternate導入時の関連7件 | 7 passed、358.65秒、14 logs STOPPED/panicなし。`.pytest-results/audio-alternates-related*` |
| 直前の全体基準 f1c9f29 | 34 passed、1631.13秒、68 logs STOPPED/panicなし。`.pytest-results/wide-loan-all*` |
| S2/P4 | 外部ring長指定後のAudioPlayback compile成功、各29 sourceの依存/nmに旧stack/SDK fallbackなし。Vendor＋CCIDの基準は033f2d8。USB実機未確認 |
| examples | 旧6例×S3/S2/P4の18ビルド成功。新AudioPlaybackは3 profileともcompile/ELF/FQBN/nm確認成功 |

最新関連7件は、4 PCM形状×2規格の16 ISO交換、14回の旧ring poison、frame境界/状態照会、
32個の不正alternate要求、13 rate・158 byte RANGE、停止後の再構成、実bus休止、core要求を含む。
開始816＋設定14取得点の失敗回復、heap346704→346704を確認した。同じ条件を全38件でも確認した。
全体監査済み。read-only scriptは`/tmp/espusb-v3-audio-alternates-all-audit.py`、
集計は`.pytest-results/audio-alternates-all-audit.txt`、原本は`/tmp/pytest-embedded/2026-10-28_22-37-36-110392/`。

## 試験の運用

`validation/`だけを開発試験の入口とする。旧tests/examplesは新APIの合否基準やhelperとして使わない。
plugin配布版1.10.0、local `698bb29`、Core3.3.12、EspUsbHost2.10.1（必要なprobeのみ）。
標準combo `s3: primary=s3_host,device=s3_device`、`--noconftest --maxfail=1`。
独自conftest/pluginへhookを移さない。不足機能はplugin側の拡張相談を優先する。

```sh
cd /home/mt/dev/EspUsbDevice-v3/validation
uv sync --locked
uv run --locked --env-file .env pytest --combo all --collect-only -q
uv run --locked --env-file .env pytest --combo all -q -rs --junitxml=.pytest-results/v3-all.xml
```

実機pytestは一つずつ。対応するbuild/test中は製品とprobeを変更しない。
別exampleのcompileは`build/examples/<profile>/<sketch>/`へ分離し、実機suiteの生成物と共有しない。
全38件の実行中の追加作業はAudioPlayback例と文書だけで、製品/probeは033f2d8に固定した。

## 次の単位と未完了

1. 全38件とAudioPlayback例の3 profileビルド、回収/所有権・依存の監査は完了。
2. 外部ringの最大frame境界・guardと能力表拒否/修正後の再開始は確認済み。
   外部pointerに提供byte長2項目を追加し、長さ不足を開始前に検出。Audio本体304 byte（+8）、新規取得点なし。
   configureは他クラスと同様に戻り値esp_err_tで確認する。
   代表複合はHID＋CDC＋Vendorの実通信に加え、CDC＋MSCの同時往復/メディア異常間の継続を確認した。
   Audio＋CDCの4構成も実通信し、疎なIN番号とTX FIFO番号の混同を修正。追加map/取得点なし。
   次は修正後の全40件を固定ソースで実行し、取消し/STALL/reset/ISOの回帰・80 serial・依存を監査する。
3. CCIDのmessage>=MPSは仕様上必要。P4 HSでは既定320がMPS512を満たさないため512以上の設定が必要。
   Audioの方向内channel数変更、PCM形状ごとに異なるrate集合、borrowed PCM/外部codec clockは別の設計対象。
4. OS互換、remote wakeup、長時間、S2/P4の実機FS/HSは後段。S31は現SDK非対応の新規port対象。
   任意の全EP0順序や全競合順序まで検証済みとは扱わない。
5. 共有toolkit管理のrelease workflowは未変更。公開前にvalidation/legacyのarchive範囲をtoolkit側で調整する。
   新examplesはまだ全クラスcatalogではない。古い非V3資料は履歴として残し、全リンクの修復済みとはしない。

初期実機はS3–S3。将来の常駐案はS3二台＋S31–HS–P4–FS–S2の計五台。
P4は一方Device/他方Hostを想定し、二つのDevice controllerの同時利用を約束しない。
