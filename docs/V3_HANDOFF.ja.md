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

## Vendor初期実装の検証記録

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
結果はv3全体の完了を意味しない。他クラスの固定資源、cancelとreset/endの競合、
全取得点の故障注入、P4 cache/HS、S2/S31は未完了。direct上限の成功転送は小さい長さの範囲だけで、
65472 byteそのもののwire転送は今回の試験には含めていない。

## 次に進める作業

1. descriptor writerのサイズ照会を導入し、configuration/HID report/BOS/MS OSの固定領域を撤去する。
   Function registryの固定上限は下記で撤去済み。
2. CDCのcontrol要求、通知、reset/endと借用取消しの組合せを必要範囲で拡張する。
   Vendor/CDCの共通転送と基本controlは下記で実装・検証済みだが、全OS/control要求の網羅ではない。
3. Audioの最小周期経路と併用して共通設計を見直し、HIDと残クラスを独自化する。
4. EP0/device state/DCDを置換し、TinyUSB依存を除去する。
   16 bitの投稿長も暫定adapterの制約であり、v3の最終上限として固定しない。

Sol Mediumを基本とし、競合・EP0/DCD等の難所だけHighを検討する。
API変更ごとに利点・不利益、既存利用への影響、移行例、実験結果を更新する。
P4/S2/S31は内部adapter/能力表で拡張し、S3のprofileやMPSを公開APIの定数に埋め込まない。
新試験は`validation/`だけで、conftestや独自pytest hookを追加しない。

## 投稿式RXの追加結果

`vendor-posted-rx.xml`、1 passed、56.21秒。従来の86往復等に加え、borrowed RXの
成功13件（callback再投稿2件を含む）、部分受信reset 1件、部分受信end 1件を確認した。
容量、実受信量、pointer、payload、境界canary、Busy/不正容量を検証した。
満杯、short/ZLP、MPS倍数dataの後に別途ZLPを送る場合を区別している。
copy構成とring/packetを持たないborrowed専用構成それぞれ100回の開始終了で、heapは318124→318124 byte。
serial原本は`/tmp/pytest-embedded/2026-10-26_03-44-43-979896/test_vendor_roundtrip/`。
投稿APIは呼出しごとのEspUsbTransferSubmissionを返す。初期v3のlastDirectErrorは廃止し、
完了callbackをonDirectTxComplete/onDirectRxCompleteへ整理した。現在のAPIはVendorガイドを参照。

## 個別取消しと遅延eventの追加結果

`vendor-cancel-all.xml`、2 passed、0 skipped、108.37秒（2026-10-26）。
公開API試験に20回のTX/RX同時取消しを追加した。交互に64 byteの部分送受信を行い、
Cancelledが一度だけ返ること、取消し直後の送受信、canary、古いidの取消し拒否を検証した。
別caseでは古い完了event 16件とevent queue満杯を注入し、fence予約の再試行後、
取消しcallbackが再投稿した新RX領域を古い完了が返さないことを確認した。
両caseのSTOPPEDまで確認。serial原本は`/tmp/pytest-embedded/2026-10-26_04-24-11-658593/`。

取消しはcaller所有のworker jobでDMA停止を依頼し、FIFO fenceで古い完了を処理してからbufferを返す。
取消しごとのheap確保はない。DWC2のDATA PIDは保持し、STALLによる取消しを行わない。
投稿と完了の非0 idで古い取消し要求を識別する。公開契約はVendorガイドを参照。

試験設定は標準`--noconftest --maxfail=1`。USB停止失敗もtest失敗にし、次caseを止める。
独自cleanup hookを追加する必要はなかった。公開probeと内部故障注入variantは分けてある。
今回の成功で他SoCやcancel/reset/end競合の網羅まで合格したとは扱わない。

copy write専用の新build probeもcompile成功。nmで`EspUsbDeviceVendor::write`の存在と、
`cancelDirect*`/`cancelPump`/`dcd_edpt_abort`/`vendord_*`の不在を確認した。
未使用取消しcodeのリンク除外であり、instance内の状態保持や共通runtimeの固定費までゼロという主張ではない。

## 取消しとreset/endの競合試験

`vendor-cancel-lifecycle.xml`、1 passed、90.00秒。S3–S3で次の3条件を各3回確認した。

- workerを止め、取消し要求後に実際のHost bus resetを発生。DCD ISRがresetをqueueへ入れたことを確認してworkerを再開する。TX/RXともBusResetで一度だけ返却。
- 取消しjobの実行前にendを要求。停止処理開始後にworkerを再開し、TX/RXともStoppedで返却。
- DMA停止後、返却fenceの実行前にendを要求。残るfenceを破棄し、TX/RXともStoppedで一度だけ返却。

全条件で64 byte部分転送を入れ、返却後のcanary、同じinstanceで再接続した後の送受信、
古いidの拒否を確認した。旧workerのqueue/jobが次のbeginへ残らないこともこの範囲で確認した。
実行variantだけがprivate hookを使う。conftestやpytest hookは追加していない。
serial原本は`/tmp/pytest-embedded/2026-10-26_04-31-54-532422/test_cancel_lifecycle/`。
これは全ての競合順序を網羅する主張ではなく、上記で明示した返却前後の条件の結果である。

## DWC2の実カウンタ幅と大きな転送

65408 byteのTXを実際に流すと、4096 byteのHost readが一度成功した後、次のreadがtimeoutした。
S3実機のGHWCFG3は`0x00c804b5`（転送長16 bit、packet数7 bit）。
共通register型のpacket数10 bitを実装能力とみなす計算が誤りだった。
失敗の原本は`/tmp/pytest-embedded/2026-10-26_04-37-25-702985/test_transfer_limits/`。

DMA armを能力レジスタのbyte/packet上限で分割し、中間armでは完了通知を出さないよう修正した。
RXはchunkごとにcacheをinvalidateし、short/ZLPで早く終わった時だけ途中の合計量で返却する。
abortも進行中chunkだけをinvalidateし、移動後pointerへ元の全長を適用しない。
TX上限は暫定adapterの16 bit長から65535 byte、RXはMPS/64単位へ切り下げて65472 byteとなる。
最初の修正確認は`vendor-transfer-limits.xml`、1 passed、40.02秒。
その後、8128/16256 byteの分割境界前後、大きな転送後の1 byte送信、
8192 byteまで部分送受信した後の取消しを追加した。最終結果は下記に記録する。

変更した暫定TinyUSB fileの差分は`third_party/tinyusb/LOCAL_PATCHES.json`へ記録し、
`python3 tools/verify_tinyusb_vendor.py`で51選択file・15 compiled sourcesの一致を確認した。
最終的なTinyUSB全置換の方針は変更していない。

最終実行: `vendor-segmented-all.xml`、**4 passed、0 skipped、230.70秒**。
`cancel_fence`、`cancel_lifecycle`、`transfer_limits`、`vendor_roundtrip`を標準入口で順次実行し、
全caseで両boardのSTOPPEDとpanicなしを確認した。大きな転送のcaseは53件の返却を検証した。
全serial原本は`/tmp/pytest-embedded/2026-10-26_04-40-58-232822/`。
JUnit、transcript、serialコピー、初回の失敗ログは`validation/.pytest-results/`に保存した。
これはS3–S3のVendor経路の結果であり、CDC等の置換やv3全体の完了を意味しない。

## Vendor/registryの確保失敗注入

`vendor-allocation-failure.xml`、1 passed、85.16秒。内部`EspUsbMemory`境界を試験sketchの
strong定義で差し替え、通常/Callback RX/外部ring/Borrowed専用/2 instanceの計33取得点を
順に失敗させた。state、ring、DMA packet、driver table/source tableが対象。
各回でNoMemory、設定保持、capacity=0、全所有領域の解放、再開始成功を確認した。
33回復後のheap一致・heap integrity・外部ring誤解放なしと、実USBの1/64/129 byte往復も確認。
通常buildはweak定義のmalloc/aligned DMA確保を使う。公開API・build_opt.h・conftestへの試験設定追加はない。
Device descriptor、RTOS/PHY、他クラスの全取得点まで検証したものではない。

## CDCと共通bulk streamの実装

`EspUsbDeviceBulkStream.cpp`へVendorのring/DMA/借用/返却/取消しを切り出し、CDCも同じ経路を使う。
`EspUsbDeviceVendor.cpp`はVendor descriptor/controlの固有処理、
`EspUsbDeviceCdcSerial.cpp`はCDC descriptor/line coding/state/breakの固有処理となる。
CFG_TUD_CDC=0にして旧CDC driver、固定instance配列、固定FIFOを外した。
共通型、コピー終端policy、snapshotと移行は[V3_CDC_API.ja.md](V3_CDC_API.ja.md)。

`cdc-shared-bulk.xml`の初回確認は1 passed、45.53秒。2 CDCの容量を小/大/非power-of-twoへ変更し、
両portの42往復、借用RX/TXの7往復、CDC+Vendor+HID構成の6往復とkey押下/解放を確認した。
S3のIN endpoint予算から、2 CDCとCDC+Vendor+HIDは別構成にしている。
CDC+Vendorも含む確保失敗を追加し、46失敗/46回復、heap 321484→321484 byteを確認した。
copy stream末尾のEndWithShortPacketはVendor側でも明示設定し、Host readへZLPを含む終端を確認する。
全v3回帰とリンク除外確認の結果は下記へ記録する。

最終確認: `cdc-vendor-all.xml`、**6 passed、0 skipped、340.68秒**。
allocation_failure/cancel_fence/cancel_lifecycle/cdc_stream/transfer_limits/vendor_roundtripを順次実行した。
全caseで両boardのSTOPPED、panicなしを確認。原本は
`/tmp/pytest-embedded/2026-10-26_05-04-04-756554/`、JUnit/transcript/serialコピーは`.pytest-results/`。

Device only / Vendor copy / CDC copyの3 build probeもcompile成功。nmで以下を確認した。

- Device onlyにはVendor/CDC/BulkStream/BulkState実装がない。
- Vendor copyにはCDC固有処理がなく、CDC copyにはVendor固有処理がない。
- 両copy probeに未使用cancelDirect/cancelPump/dcd_edpt_abortはない。
- 全probeにTinyUSBのcdcd/tud_cdc_n/vendord実装がない。

旧`CFG_TUD_CDC_TX_BUFSIZE=2048`を指定するbuildは、EspUsbBulkStreamConfigへの移行案内で拒否することも確認した。
`python3 tools/verify_tinyusb_vendor.py`も51 file / 15 compiled sourcesで一致。

次の具体的作業は、RXを使わない送信専用modeを明示し、資源0のためにdummy RX callbackを要求しない契約へ整理すること、
およびFunction registry/descriptor容量の動的化である。未検証のOS/SoCは上記と同じで、v3全体は未完了。

## 送信専用modeの追加

RX Disabledでring/packet/callbackを不要にした。CDC/Vendorの送信専用copy/borrowedを、
1/63/64/65/127/128 byteで確認。借用TXはZLPの完了まで待って返却する。
HostのVendor readもMPS倍数時にはZLPを消費する長さで要求するようprobeを修正した。
`tx-only.xml`はCDC suite 1 passed、42.26秒。確保失敗suiteも追加の8取得点を含む54失敗/54回復を確認した
（原本`/tmp/pytest-embedded/2026-10-26_05-14-53-103287/test_allocation_failure/`）。
次は登録/descriptorの固定容量の撤去。

## Function registryの動的化

固定MAX_CLASSES=6を撤去。constructorはFunction内のlinkへ登録し、beginで実数に応じた
検索/名前の一覧を一度確保する。end/開始失敗で解放し、未登録Functionを黙って省略しない。
GET_DESCRIPTORでは稼働中のdescriptorを再構築せず、worker起動前の公開時点から固定する。
同じ種類/custom HIDのreport ID衝突も見つかったため、preferred IDの予約と空きIDの割当へ変更した。
詳細は[V3_FUNCTION_REGISTRY.ja.md](V3_FUNCTION_REGISTRY.ja.md)。

最初の確認は`function-registry.xml`、2 passed、75.41秒。
登録/削除/名前/再登録の20反復でheap 324576→324576 byte、
7 Functionの実USB reportと、一覧を含む62取得点の確保失敗/回復を確認した。
その後、同じpreferred IDを指定したHIDの再割当も加えて全回帰を実行中。
configuration/HID report/BOS/MS OSの固定領域はまだ残っており、次はdescriptorのサイズ照会と動的確保へ進める。

最終の標準実行: `dynamic-registry-all.xml`、**7 passed、0 skipped、360.78秒**。
全caseの両board STOPPEDとpanicなしを確認した。原本は
`/tmp/pytest-embedded/2026-10-26_07-03-54-332264/`、コピーとJUnit/transcriptは`.pytest-results/`。
最後に、未登録/数超過がbegin前のdescriptor getterでも拒否される条件を追加し、
`function-registry-final.xml`のfocused実行でも1 passed、31.65秒を確認した。

リンク確認: Device only / Vendor copy / CDC copyの3 probeを再compileし、
未使用Function固有処理・取消し・TinyUSB CDC/Vendorの不在をnmで確認した。
最後のfocused実行には、未登録HIDの送信拒否とその破棄後の通信継続も含む。
`python3 tools/verify_tinyusb_vendor.py`は51 file / 15 compiled sourcesで一致。
次はFunction descriptorのサイズ照会契約と、固定descriptor領域の撤去を進める。
