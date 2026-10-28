# v3開発の引き渡し

更新: 2026-10-29。作業場所`/home/mt/dev/EspUsbDevice-v3`、branch `v3`。
製品commit `033f2d8`（ISO OUT回復は`4480513`）。metadataは3.0.0、未release。
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
- ISO OUTの間欠送信で最初のpacketだけ受信する問題を実測し、4480513でincomplete OUTをDMA停止後に返却・再arm。
- 未選択Functionのstateやkeymapを共通表で引き込まない。Vendor単独ELFとS2/P4 Vendor＋CCIDで確認する。

## 最新の検証範囲

| 単位 | 結果・記録 |
| --- | --- |
| 製品033f2d8の全38件 | **実行中、完了結果は未記録**。`.pytest-results/audio-alternates-all.xml`、transcript `/tmp/espusb-v3-audio-alternates-all.txt` |
| 最新Audio/core関連7件 | 7 passed、358.65秒、14 logs STOPPED/panicなし。`.pytest-results/audio-alternates-related*` |
| 直前の全体基準 f1c9f29 | 34 passed、1631.13秒、68 logs STOPPED/panicなし。`.pytest-results/wide-loan-all*` |
| S2/P4 | 最新製品のcompile成功、全29 sourceの依存/nmに旧stack/SDK fallbackなし。USB実機は未確認 |
| examples | 旧6例×S3/S2/P4の18ビルド成功。新AudioPlaybackは3 profileともcompile/ELF/FQBN/nm確認成功 |

最新関連7件は、4 PCM形状×2規格の16 ISO交換、14回の旧ring poison、frame境界/状態照会、
32個の不正alternate要求、13 rate・158 byte RANGE、停止後の再構成、実bus休止、core要求を含む。
開始816＋設定14取得点の失敗回復、heap346704→346704を確認した。各対象の範囲を全38件と混同しない。
全体監査用のread-only scriptは`/tmp/espusb-v3-audio-alternates-all-audit.py`。
XMLの最終成功後に実行し、実行中の部分結果で上書きしない。

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
全件実行中の追加作業はAudioPlayback例と文書だけで、製品/probeは033f2d8に固定している。

## 次の単位と未完了

1. 全38件の終了・76 serial・回収/所有権・依存を監査し、最新基準を記録する。
2. AudioPlayback例の3 profileビルドは完了。次は外部ringの最大frame境界や能力表拒否の不足を確認する。
3. CCIDのmessage>=MPSは仕様上必要。P4 HSでは既定320がMPS512を満たさないため512以上の設定が必要。
   Audioの方向内channel数変更、PCM形状ごとに異なるrate集合、borrowed PCM/外部codec clockは別の設計対象。
4. OS互換、remote wakeup、長時間、S2/P4の実機FS/HSは後段。S31は現SDK非対応の新規port対象。
   任意の全EP0順序や全競合順序まで検証済みとは扱わない。
5. 共有toolkit管理のrelease workflowは未変更。公開前にvalidation/legacyのarchive範囲をtoolkit側で調整する。
   新examplesはまだ全クラスcatalogではない。古い非V3資料は履歴として残し、全リンクの修復済みとはしない。

初期実機はS3–S3。将来の常駐案はS3二台＋S31–HS–P4–FS–S2の計五台。
P4は一方Device/他方Hostを想定し、二つのDevice controllerの同時利用を約束しない。
