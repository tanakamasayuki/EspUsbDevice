# v3 独立試験とAPI整理の引き渡し

2026-10-26。開発の入口を既存test/sampleから切り離し、S3–S3だけの独立環境に変更した。
最初にTinyUSB制約で不自然になっている利用手順を改善する。汎用registryの先行完成や
旧testへの整合維持を最初の目的にしない。製品の公開APIとスタック本体はまだ変更していない。

## 作業場所と優先順位

- branch: `v3`、worktree: `/home/mt/dev/EspUsbDevice-v3`。
- 製品コードの基準: EspUsbDevice 2.6.1、`b1094cd577b899cb90f9ac608a2b864d38b92464`。
- 最初に[API変更の判断](V3_API_DECISIONS.ja.md)と[新試験のREADME](../validation/README.ja.md)を読む。
- 所有権の不変条件は[V3_ARCHITECTURE.ja.md](V3_ARCHITECTURE.ja.md)、実測条件は[V3_EXPERIMENTS.ja.md](V3_EXPERIMENTS.ja.md)。
- 最終的な全置換・全機種の範囲は[V3_DEVELOPMENT_PLAN.ja.md](V3_DEVELOPMENT_PLAN.ja.md)。

基本方針は明示的なDevice/Function/config/beginと、使う機能にだけ費用を持たせること。
API破壊は許容するが、各変更で利点、不利益、既存利用への影響、移行例を記録する。
通常の使い方や自然な名前まで変更することは目的にしない。CH32RVはAPI調整相手、OEPは利用例の一つ。

## 試験環境の切り離し

新規環境は`validation/`。独立pyproject/lock/venvを持ち、`--noconftest`を標準設定にする。
既存の`tests/`と`examples/`はv2資産として残すが、v3の試験収集・helper・sketch・API合否基準に使わない。
それらの過去CIを新API開発のgateとして流用しない。新しいCIが必要になったらvalidationだけを対象にする。

前回commit `09cf221`のtests変更は撤去し、旧testsを分岐元の内容に戻した。
`usb_test_matrix.py`、`--usb-require`、local profile用hook、独自結果監査を新環境へ移植していない。
前回の56 passed等は当時の旧環境の結果で、新環境の合格数ではない。過去記録は同commitで参照できる。

plugin標準のprofile/port、device lock、dut/peers、build/upload、JUnitを使う。
必要機能が不足するならplugin側への追加依頼を優先し、conftestは最後の手段とする。
別helperや独自pluginへhookを移して同じ仕組みを維持することもしない。
現在の追加依頼候補は新試験READMEに記載。外部への依頼やpluginへの変更はまだ行っていない。

## 新規S3–S3試験の結果

| 項目 | 条件・結果 |
| --- | --- |
| suite | `validation/s3/vendor_roundtrip`、新規Host/Device sketchとtest関数一本 |
| profile | `s3_host` / `s3_device`、各一つだけ |
| Arduino Core / Host | 3.3.12 / EspUsbHost 2.10.1 |
| plugin / pytest-embedded / pytest | 1.9.0 / 2.9.3 / 9.1.1、Python 3.13.16 |
| Host / Deviceの制御port | by-id `esp32-s3-d0cf1359101c` / `esp32-s3-d0cf1358fd94` |
| USB link | S3 OTG同士、FS、bulk IN/OUT MPS=64をHostで確認 |
| 収集 | 1 test、既存testやconftestを読まない |
| build/upload/test | 1 passed、0 skipped、0 failed/error、83.41秒 |
| 内容 | 1/63/64/65/127/128/129 byteの往復、全byte比較、Device側受信量、両board停止 |
| 結果 | `validation/.pytest-results/s3-vendor.xml` |
| serial log | `/tmp/pytest-embedded/2026-10-25_21-57-49-966383/test_vendor_roundtrip/`、両側のSTOPPEDまで確認 |

開始・停止はtest本文から普通のsketchコマンドで明示する。自動fixtureやhookではない。
新probeは現行公開APIだけを使い、build_opt.h、TinyUSB内部API、旧test helperを使わない。
現在の製品コードには短いVendor返信にflushが必要なので、その操作を明記している。
この成功は動的bufferや新direct APIの実装完了を意味しない。P4/S2/S31は今回実行していない。

```sh
cd /home/mt/dev/EspUsbDevice-v3/validation
uv sync --locked
uv run --env-file .env pytest --collect-only -q
uv run --env-file .env pytest -q -rs --junitxml=.pytest-results/s3-vendor.xml
```

終了値0だけでは全件skipを合格と区別できない。現時点では今回の結果が1 passedであることを確認する。
この標準機能の追加はpluginへの依頼候補とし、独自gateを再実装しない。

## Solの次の作業

1. **短いwriteを自然にする。** 現Vendorの手動flushとCDCの自動送出の違いを確認し、
   Vendor自動送出と明示batchの候補を新probeで比較する。write受理とwire完了を混同しない。
2. **容量とmodeをinstance設定へ。** 再ビルドなしで小→大→小の設定変更を実装する。
   必要なregistry・allocator・lifecycleをこの縦の経路と一緒に作る。失敗rollbackと未使用費用を確認する。
3. **copy/borrowedの選択と返却。** directをbuild flagから解放し、切断/reset/cancelで一度だけ返す。
   通常writeに不要なticket操作を持ち込まず、高度な転送の寿命を明示できる形を比較する。
4. **CDCと複合へ展開。** 複数instance、CDC、HID、最小Audioで共通契約を確認する。
   旧sampleを合わせる作業ではなく、新契約から新規sketchと試験を書く。

Sol Mediumを基本とし、競合・EP0/DCD等の難所だけHighを検討する。
各単位はbefore/after、利点・不利益、実験結果、採用理由をAPI判断文書へ更新して完了する。
P4 cache/HS、S2/S31、五台ベンチは後段に残し、初期S3の作業を機材待ちにしない。

そのまま渡せる依頼:

> v3 worktreeでV3_API_DECISIONSとvalidation/READMEを読み、Vendorの短いwriteが
> 明示flushなしに送出される案を実装・実測する。streamのまとめ書きへの影響も確認し、
> 利点と不利益、既存利用への影響を記録して採用を判断する。試験は新規S3–S3のみ。
> 旧tests/examplesを使わず、conftestや独自pytest hookを追加しない。
