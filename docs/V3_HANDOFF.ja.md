# v3 準備結果とSolへの引き渡し

2026-10-26。準備の範囲は、新スタックを実装する前に必要な契約・実験・試験環境の整備。
製品コードはまだv2であり、新API、独自EP0/DCD、runtime buffer変更は未実装である。
APIを事前に完全固定せず、以下の単位で実装・実測して採用する。

## 作業場所と読む順序

- branch: `v3`、worktree: `/home/mt/dev/EspUsbDevice-v3`。
- 分岐元: EspUsbDevice 2.6.1、`b1094cd577b899cb90f9ac608a2b864d38b92464`。
- 元の`/home/mt/dev/EspUsbDevice`はmain。実装はv3側で続ける。
- [開発計画](V3_DEVELOPMENT_PLAN.ja.md) → [API・所有権](V3_ARCHITECTURE.ja.md) →
  [実験台帳](V3_EXPERIMENTS.ja.md) → この文書の作業A。

基本方針は明示的なDevice/Function/config/beginと、使う機能にだけ費用を持たせること。
作業途中も最終APIも互換性破壊を許容する。旧API shimは作らない。
CH32RVはAPI調整相手、OEPは多くの利用例の一つ。v2性能比較を準備条件にしない。

## 準備で実施したこと

- pluginを1.6.0から1.9.0へ更新しlock。最新ローカルguideとhook実装を確認した。
- `usb_test_matrix.py`へ実結線と必須caseを集約。peer/loopbackのscopeだけにhookを適用。
- local Hostは`--usb-local-host`で有効化。default allではreleased組だけを実行する。
- `--usb-require`で今回のsetup/call/teardown成功を監査。全件skipや古いstateは合格にしない。
  予期しないserial監査行も必須実行の終了値を失敗にする。
- USB停止の応答不足を警告からteardown errorへ変更。残りのboardも停止を試み、後続試験を止める。
- parameter付きnodeidでもMSC/DFU等の既知serialログ許可が一致するよう修正。
- MIDI cable試験をreleased Host 2.10.1でも実行可能にした。local-onlyの古い制限を撤去。
- APIの所有・状態遷移・context・エラー・copy/direct・終端・再構成を出発案として整理。
  各クラスのbuild定数をどのAPI/自動算出へ移すかと、E0〜E9の判定手順を作成。

## 試験環境

| 項目 | 実施条件 |
| --- | --- |
| Arduino CLI | 1.5.1、commit 01f3d4f2b |
| Arduino-ESP32 | sketch profileで3.3.12指定 |
| EspUsbHost | profileで2.10.1指定 |
| Python / pytest | 3.13.16 / 9.1.1 |
| pytest-embedded / plugin | 2.8.1 / 1.9.0（PyPI、tests/uv.lock） |
| uv | 0.12.23。lockの大きな差分の大部分は形式revision更新 |
| S3 Host / Device | by-id `esp32-s3-d0cf1359101c` / `esp32-s3-d0cf1358fd94` |
| P4 loopback | by-id `esp32-p4-e8f60ae0aa24` |
| S2 / S31 | この準備では未設置・未検証 |

S3のOTG同士、P4のHS/FSコントローラ間loopbackという既存配線で試験した。
P4 loopbackのlinkはFSで、HS速度の検証ではない。board revision、connector/VBUSの回路図照合は
五台ベンチbring-up時の記録項目に残す。現在の配線成功だけで将来の役割交代を保証しない。
`.env`はgit管理外。旧コピーにあった存在しない`TEST_SERIAL_PORT_ESP32S3`だけをv3側で除き、
未設置single profileへ別の不明boardを割り当てていない。

## 実施結果

| 試験 | 結果 | artifact（tests/基準） |
| --- | --- | --- |
| 全profile収集 | 233 items、適用条件のskip表示あり。port接続を証明するものではない | `.pytest-results/v3-prep/collect.txt` |
| unit | 51 passed | `.pytest-results/v3-prep/unit.xml` |
| 必須S3をlocal指定で全件skip | 3 skipped、終了値1を確認 | `.pytest-results/v3-prep/all-skip-negative.xml` |
| S3 Keyboard/CDC/複合、build/upload/test | 3 passed、9 skipped、必須集合成功。6 serial logsに異常なし | `.pytest-results/v3-prep/s3-smoke.xml` |
| P4 loopback CDC、build/upload/test | 1 passed、1 skipped、必須集合成功。1 serial logに異常なし | `.pytest-results/v3-prep/p4-smoke.xml` |
| S3 MIDI複数cable、released Host | 1 passed、3 skipped。2 serial logsに異常なし | `.pytest-results/v3-prep/midi.xml` |
| unit＋上記S3/P4を一つのall実行、test mode | 56 passed、13 skipped、両必須集合成功。9 serial logsに異常なし、未完了0 | `.pytest-results/v3-prep/combined.xml` |
| profile別portを全て外した必須S3 | 12 skipped、終了値1。機材不足を合格にしない | `.pytest-results/v3-prep/no-equipment-negative.xml` |
| build-onlyへ必須実機gateを指定 | 終了値4のusage error | `.pytest-results/v3-prep/build-gate-negative.txt` |
| build-only収集、local有効 | Keyboardの4組を除外せず収集、終了値0 | `.pytest-results/v3-prep/build-collect.txt` |

上記は試験環境と現行通信を確認したもので、v3転送契約の合格ではない。
新スタックに対する測定はE1以降で実施する。比較benchmarkは行っていない。
全suiteの実機回帰、全example build、OS互換、S2/S31、P4実HSは今回の成功範囲に含めない。
unitはbuffer返却の新実装を検査するものではなく、現行unitと今回追加した試験判定・停止処理を検査する。
artifactはgit管理外で、結果要約はこの文書へ保存した。実行transcriptとserial logのコピーも
`.pytest-results/v3-prep/`へ集約している。再現に必要なコードとlockはv3ブランチへ記録する。

## 再実行

`tests/`から実行する。実機pytestは一つずつ起動する。

```sh
uv sync --locked
uv run pytest unit -q
uv run --env-file .env pytest --profiles=all --peer-profiles=all --collect-only -q

# 初期段階の必須実機。通常はこちらでbuildも実行する。
uv run --env-file .env pytest peer/hid_keyboard peer/usb_serial peer/composite_hid_cdc loopback/usb_serial --profiles=all --peer-profiles=all --usb-require=s3-smoke --usb-require=p4-smoke -rs --junitxml=.pytest-results/smoke.xml

# 今回最後に実行した逐次確認。先に各sketchをbuild済み。
uv run --env-file .env pytest unit peer/hid_keyboard peer/usb_serial peer/composite_hid_cdc peer/usb_midi_cables loopback/usb_serial --profiles=all --peer-profiles=all --run-mode=test --usb-require=s3-smoke --usb-require=p4-smoke -x -rs --junitxml=.pytest-results/v3-prep/combined.xml

# 全体。現在の必須集合以外も実行するが、この全suiteは今回未実行。
uv run --env-file .env pytest --profiles=all --peer-profiles=all --usb-require=s3-smoke --usb-require=p4-smoke -rs --junitxml=.pytest-results/all.xml
```

build済みでコード・依存に変更がない確認だけは`--run-mode=test`を使える。両端uploadは省かれない。
local Hostを調べる場合は`--profile=s3_peer_local --peer-profile=device:s3_peer_local --usb-local-host`。
released Host必須集合の代わりにはならない。build-onlyに`--usb-require`を付けるとusage errorにする。
全件skipの負例は同じS3三ディレクトリへ`--profile=s3_peer_local --usb-require=s3-smoke`を指定する。

## Solの次の作業単位

| 単位 | 実装範囲 | 完了条件・モデル運用 |
| --- | --- | --- |
| A registry/lifecycle | 最小公開header、登録Functionだけのlist、Stopped/Starting/Started/Stopping、descriptor計画、allocator、結果型。fake DCDを用意 | E1のinclude/宣言/確保失敗、E2の一度だけの返却をhost unitで確認。Sol Medium、所有権が複雑ならHigh |
| B Keyboard | 最小EP0/endpoint境界とKeyboardを通す。暫定TinyUSB app-driver adapterは内部のみ | S3 Keyboardで列挙・raw/基本操作・LED・停止/再開始。mapに無関係classが残る原因を除く |
| C CDCと複合 | instance別ring容量、複数CDC、Print境界、Keyboard+CDC | S3 CDC/複合、E1容量変更/100回開始終了、E2競合。ここで公開例と契約を一度レビュー |
| D Vendor | copy/borrowed RX/TX、ticket、終端、cancel | E2/E3。build_optなしでmode切替。S3成功後P4 E4でDMA/cacheを確認。難所のみSol High |
| E 最小Audio | PCM境界、alt切替、playback/captureとfeedback | E5。bulkと周期転送の両方で設計が成立してから全classへ展開 |
| F 残クラス・DCD | E7の移行表に沿って展開。EP0/DCD置換は別commit群 | E6/E8/E9と計画の段階4〜6。S31 Host未対応をDeviceの不具合と混同しない |

Aでクラスなしの別ライブラリを完成させることに時間を使わない。Bへ接続するための最小骨格に留める。
暫定adapterでcancel安全性やunused費用が成立しなければ、最小EP0/DCDをBへ前倒しできる。
API名・既定容量・queue値は実験結果で決め、互換性のための確認待ちを挟まない。
各単位の末尾に採用値・根拠・試験コマンド・未対応クラスを本書と実験台帳へ更新する。

作業量はA〜CとD/Eを別に見積もる。A〜Cの実績が出るまでは全体11〜20人週という以前の幅を
日程約束やモデル稼働時間へ換算しない。クラス実装よりEP0/DMA・OS差分・機材bring-upに不確実性がある。
Sol Mediumで機能単位を進め、停滞時はログを増やして問題を分離し、必要箇所だけHigh/現モデルへ切り替える。

そのまま渡せる最初の依頼:

> v3 worktreeのV3_HANDOFFとV3_ARCHITECTUREを読み、作業Aを実装する。
> 旧API互換層は作らず、必要Functionだけを登録し、begin失敗のrollbackとendの所有権を実装する。
> E1/E2のうち機材なしで検証できる部分を実行し、最小headerで基本例・複合例の構文を確認する。
> 結果を記録してBのS3 Keyboardへ接続できる状態にする。未実装APIを成功扱いにしない。

## 今後の実機待ちと判断

常駐はS3二台＋`S31 — HS — P4 — FS — S2`の五台を前提にする。
S2/S31の購入board型番・connector/VBUS確認、S31 Core/Host成立、PC HS/OS検証の機会は未確定。
それぞれE8/E9に手順と条件を記載しており、S3の実装開始を妨げない。
ライブラリ全体の仕様を先に固定したり、まだない実機の成功を推定したりせず進める。
