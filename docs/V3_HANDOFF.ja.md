# v3 独立試験とAPI整理の引き渡し

2026-10-26。開発の入口を既存test/sampleから切り離し、S3–S3だけの独立環境に変更した。
最初にTinyUSB制約で不自然になっている利用手順を改善する。汎用registryの先行完成や
旧testへの整合維持を最初の目的にしない。Vendorの公開APIとクラス処理を更新した。共通EP0/DCDと他クラスはまだ暫定TinyUSBを使う。

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

## 初期S3–S3 probeの記録

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
初期probeでは短いVendor返信にflushが必要だった。現在は自動送出へ移行した。
この成功は動的bufferや新direct APIの実装完了を意味しない。P4/S2/S31は今回実行していない。

```sh
cd /home/mt/dev/EspUsbDevice-v3/validation
uv sync --locked
uv run --env-file .env pytest --collect-only -q
uv run --env-file .env pytest -q -rs --junitxml=.pytest-results/s3-vendor.xml
```

終了値0だけでは全件skipを合格と区別できない。現時点では今回の結果が1 passedであることを確認する。
この標準機能の追加はpluginへの依頼候補とし、独自gateを再実装しない。

## 現在の実装と検証

Vendorを`src/EspUsbDeviceVendor.cpp`へ切り出し、TinyUSB Vendor driverを無効化した。
instanceごとの容量、外部ring、自動送出/明示flush、callback RX、copyなしTXをAPIで選べる。
借用TXは成功/reset/endでstatus付き返却を行う。実装中のAPIと移行は[V3_VENDOR_API.ja.md](V3_VENDOR_API.ja.md)。
mainの文書commitもmerge済み（453a26a）。旧test/sampleの整合維持を開発gateにしていない。

2026-10-26の最終確認: `vendor-end-contract.xml`、1 passed、0 skipped、52.30秒。
新規S3–S3の一つのtest内で86往復、明示batchの1受信を確認した。

- 小→大→小の容量変更、非power-of-twoの外部ring、範囲外canary、設定拒否と確保失敗後のrollback。
- 100回begin/endで内部heapが318836→318836 byte。終了後の実効capacityは0。
- borrowed TXの14成功、resetによる1返却、endによる1返却。pointer、status、既知/不明の実送信量、通知数を検証。
- endの成功/拒否を結果で識別。USB workerからの16回の再入終了要求を拒否し、正常終了後のerror=0を確認。
- MPS倍数のZLP終端とContinue、Busy・alignment・長さ・packet数制限の拒否、稼働中callback変更の拒否。
- 2 Vendor instanceの独立容量とinterface選択、RX ringなしのcallback RX。
- DeviceだけのbuildではVendor実装がリンクされない。Vendor使用時もvendord/TinyUSB Vendor配列は残らない。

`validation/.pytest-results/vendor-end-contract.xml`と同ディレクトリのtranscript/serialコピーを参照。
serial原本は`/tmp/pytest-embedded/2026-10-26_03-17-21-500652/test_vendor_roundtrip/`。
結果はv3全体の完了を意味しない。他クラスの固定資源、個別cancel、投稿式borrowed RX、
全取得点の故障注入、P4 cache/HS、S2/S31は未完了。direct上限の成功転送は小さい長さの範囲だけで、
65472 byteそのもののwire転送は今回の試験には含めていない。

## 次に進める作業

1. Vendorの投稿式RXと個別cancelを、転送世代/遅延eventの試験と一緒に設計・実装する。
   暫定DCDのstallだけで古い完了を安全に消せると仮定しない。必要ならDCD境界を先に置換する。
2. allocator各取得点での故障注入、callbackからの次転送、部分転送中のresetを新しい試験に追加する。
3. 共通ring/transfer契約をCDCへ展開し、HIDとの複合で確認する。必要なregistryとdescriptor容量を動的にする。
4. Audioの最小周期経路と併用し、共通設計を見直してから残クラス・EP0/DCDを置換する。

Sol Mediumを基本とし、競合・EP0/DCD等の難所だけHighを検討する。
API変更ごとに利点・不利益、既存利用への影響、移行例、実験結果を更新する。
P4/S2/S31は内部adapter/能力表で拡張し、S3のprofileやMPSを公開APIの定数に埋め込まない。
新試験は`validation/`だけで、conftestや独自pytest hookを追加しない。
