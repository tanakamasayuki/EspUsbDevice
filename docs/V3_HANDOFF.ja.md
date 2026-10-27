# v3 独立試験とAPI整理の引き渡し

最新実装は共通core/EP0の独自化（`ce6a444`）。標準combo allで全27件成功（1743.12秒）。
[core APIと未完の境界](V3_CORE_API.ja.md)を参照。TinyUSBのコンパイル対象はDWC2関連2本、
vendor選択は24 file。C ABI/headerとDWC2への依存は残る。

2026-10-26。開発の入口を既存test/sampleから切り離し、S3–S3だけの独立環境に変更した。
最初にTinyUSB制約で不自然になっている利用手順を改善する。汎用registryの先行完成や
旧testへの整合維持を最初の目的にしない。CDC/Vendor/HID/MIDI/MSC/Audio/DFU/NCM/Videoのclassを独自化した。共通EP0/core/DCDはまだTinyUSBを使う。

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

1. legacy classの固定資源とruntime/APIの制限を整理する。
   Function registry、configuration/HID report/BOS/MS OSと文字列/URLの固定領域は下記で撤去済み。
   HID classとGET_REPORT/idle、MIDI classのpacket転送は独自化済み。期限近傍とOS互換性の追加検証は残る。未完IN中のprotocol変更は末尾の専用試験で補強した。
2. CDCのcontrol要求、通知、reset/endと借用取消しの組合せを必要範囲で拡張する。
   Vendor/CDCの共通転送と基本controlは下記で実装・検証済みだが、全OS/control要求の網羅ではない。
3. Audioの最小周期経路と併用して共通設計を見直し、HIDの残制御と残クラスを独自化する。
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

## descriptor領域の動的化

Function writerにnull照会とcapacityの契約を追加し、全既存writerを対応させた。
configurationを計数後に実数分だけ確保し、FS/HSはそれぞれのwriterで出力する。
S3では通常HS/OtherSpeedを確保せず、HSの明示照会はPHYを始めない場合だけ行える。
複合HID report、BOS/MS OSも使う構成だけで実数分を確保し、end/失敗で解放する。
USB GET_DESCRIPTORとFIFO検証は完成したsnapshotを読み、再計数/確保を行わない。

MS OS setの固定二対象とrevision offsets配列も撤去し、全WinUSB対象をinterface順に構築する。
複数対象にFlatを強制する場合はInvalidArgumentで拒否する。
移行と限界は[V3_DESCRIPTOR_STORAGE.ja.md](V3_DESCRIPTOR_STORAGE.ja.md)。

`descriptor-storage.xml`は新probe+registryの2 passed、123.89秒。
FS/HS writer44条件、長さ超過/不安定な照会の拒否、heap回復、
configuration846 / MS516 / HID report366 byteの実USB列挙・通信を確認した。
`dynamic-descriptors-all.xml`の標準入口では**8 passed、0 skipped、390.92秒**。
全caseで両board STOPPED、panicなし。原本は`/tmp/pytest-embedded/2026-10-26_08-24-05-667930/`、
JUnit/transcript/serialコピーは`.pytest-results/`へ保存した。確保失敗は86取得点で回復した。

文字列scratch/URL応答やlegacy class/coreの固定資源はまだ残る。
P4/S2/S31の実機やWindowsのMS OS動作は今回の結果に含めていない。

最終のdescriptor focused実行は1 passed、28.45秒。BOS単独/併用、誤GUID拒否、
速度ごとに異なる長さ（FS18/HS27）、全bulk writerのHS packet 512を追加確認した。
VendorのFSサイズ指定がHSでも64に残らないよう、HSは512を出力する従来挙動をwriterへ移した。
Device only / Vendor copy / CDC copyの3 buildとnmも成功し、未使用クラス・取消し処理は残らない。
TinyUSB vendor treeの照合も51 file / 15 compiled sourcesで一致。

## plugin 1.10.0と結線済みcombo

descriptor変更commit `fc059b0`の区切りでpluginの新版を再確認した。
local sourceは1.9.0だったが、配布版とremoteのv1.10.0 tag
`698bb295182fc685efb3a55cd8c995c68fc3fe84`を確認し、release guideの`--combo`を採用した。
plugin自身の作業ツリーは変更せず、validationの依存下限とlockを1.10.0へ更新した。

`validation/pyproject.toml`の`arduino_combos`は`s3: primary=s3_host,device=s3_device`の一組。
標準入口は`uv run --locked --env-file .env pytest --combo all -q -rs`。
単独選択は`--combo s3`。どちらも同じ8 testを収集した。
旧test/sampleやconftestは使わず、test本文・sketch・結線・port名は変更していない。
将来は対応profileと結線済みcomboを増やし、直積や独自の除外hookを使わない。
詳細と再実行コマンドは[validation README](../validation/README.ja.md)を参照する。

実機全実行`combo-all.xml`は**8 passed、0 skipped、0 unfinished、416.75秒**。
全caseで両board STOPPED、panicなし。serial原本は
`/tmp/pytest-embedded/2026-10-26_08-58-48-522209/`、
JUnit/transcript/serialコピーは`validation/.pytest-results/combo-all*`。
今回の変更は試験環境と計画・記録のみ。製品API、sketch、旧test/sampleには変更を加えていない。

## USB文字列とWebUSB URLのsnapshot

63 byteの切り詰め/byte単位の拡張を廃止し、UTF-8からUTF-16へ変換する。
文字列はoffset表と実際のUTF-16を一つの領域に確保し、全て省略時は確保しない。
Function registry内の借用名前pointer一覧も撤去し、登録検索pointerだけにした。
WebUSB URLは有効かつ指定時だけ実数分を確保し、GET_URLは構築済みbyte列を参照する。
両者ともbegin時のsnapshotで、end/開始失敗で解放する。

長さ超過・不正UTF-8は明示エラー。URLなしのWebUSBはiLandingPage=0とし、
GET_URLのindex/方向/recipientを検証する。prefixなしURLはscheme 255として全文を保持する。
変更の利点/不利益、既存利用の移行、寿命は[V3_TEXT_DESCRIPTORS.ja.md](V3_TEXT_DESCRIPTORS.ja.md)。
focused試験`text-descriptors.xml`は2 passed、89.45秒。
日本語/絵文字、最大長、入力変更後のwire応答固定、不正要求後のEP0復帰とheap回復を確認した。

Device only / Vendor copy / CDC copyの3 build/nmは成功。
未使用クラス・取消し・TinyUSB CDC/Vendorの不在を維持し、TinyUSB tree照合も51 file / 15 sourcesで一致。
文字列/URL以外のlegacy class/core固定資源と、残クラス・EP0/DCDの置換は未完了。

最終の標準`--combo all`は`immutable-text-all.xml`、**8 passed、0 skipped、0 unfinished、408.43秒**。
全caseで両board STOPPED、panicなし。118取得点の失敗/回復と文字列なしの確保省略、
251 Functionの長い名前/string index 255も確認した。serial原本は
`/tmp/pytest-embedded/2026-10-26_11-47-05-819133/`、
JUnit/transcript/serialコピーは`validation/.pytest-results/immutable-text-all*`。

## HID classの独自化

`EspUsbHidFunction`を共通基底にし、TinyUSB HID classをCFG_TUD_HID=0で無効化した。
Report Size/Count/IDとglobal Push/Popから実数分のreport表と転送容量を求める。
複合HIDは一つのstateを共有し、INだけならOUT/controlのbufferを持たない。
独自HIDは新基底へ移行する。通常のKeyboard/Mouse等と自動IDは維持する。
Vendorの長すぎるsendInputを切り詰めず拒否し、サイズをビルド定数へ結び付けない。
HID driver自身も未使用時にはリンクされないことを3最小buildのnmで確認した。

新規`validation/s3/hid_transport`でpayload 1〜1023 byteの12条件をS3 FSで送受信した。
INの複数packet、interrupt OUT組立、control Outputのprefix有無とFeature、
無効要求後の復帰、idle値、NKRO/boot切替、LEDを検証した。
65535 byteの領域はdescriptor-onlyで確保/反復回収したが、その長さのwire試験ではない。
不正item、Push/Pop、混在IDや途中begin失敗の回収も確認する。

`usbd_control_received_bytes()`を暫定core adapterへ追加し、SET_REPORTの実受信量をDATA時に検証する。
patch manifest/provenanceへ記録し、tree照合は51 file / 15 compiled sourcesで一致。
契約と移行は[V3_HID_API.ja.md](V3_HID_API.ja.md)。この時点ではGET_REPORT snapshotとidle自動再送は未実装だった。

標準の`runtime-hid-all.xml`は**9 passed、0 skipped、0 unfinished、474.47秒**。
この時点の確保失敗は176取得点。全caseで両board STOPPED、panicなし。
serial原本は`/tmp/pytest-embedded/2026-10-26_12-18-47-546432/`、
JUnit/transcript/serialコピーは`validation/.pytest-results/runtime-hid-all*`。

その後、標準HIDクラスのdestructor先頭でendを呼び、callback/reportメンバーより先にworkerを止める順序へ補強。
`hid-transport-final.xml`は2 passed、91.43秒。稼働中のVendor HID/Keyboard破棄とprotocol callback、
解析stackの故障を含む194取得点の失敗/回復、両board STOPPED、panicなしを確認した。
原本は`/tmp/pytest-embedded/2026-10-26_12-28-54-078282/`、コピーは`.pytest-results/hid-transport-final*`。
開始成功時のstartError=ESP_OKを確認する最終HID focusedも1 passed、53.04秒。
未登録HIDがbeginで共有stateへ参加しないことと、拒否/破棄後の既存7 HID通信を
`hid-registration-final.xml`の1 passed、44.01秒で確認した。これらのserial/JUnitも`.pytest-results/`へ保存済み。

## GET_REPORT / idleとHIDの設定契約

GET_REPORTを全定義ID/typeの保存状態から応答する。制御応答は独立copyで、並行更新中も混在させない。
Input保存はsend受理時、Output/Featureは受信完了時、アプリFeatureはsetFeatureReportで更新する。
Featureはresetで保持、Input/Outputはneutralへ戻す。end/開始失敗は全領域を回収する。
control bufferはInputも含む最大長に増やし、snapshotとrelative maskは実数分だけ一括確保する。
最大長のmetadata試験はIN-only 65535 byteで行う。大きい全typeの同時確保は使用可能メモリで制限される。

idleはIDごとの完了時刻を基準に再送し、Hostが有効にした時だけcaller-owned jobを再投稿する。
Mouse等のrelative項目は完了後にzeroに戻して、同じ移動量を再送しない。
boot mouseの3 byteと通常4 byte、Keyboardのboot/NKROを確認。短い不整合boot定義はbeginで拒否する。
Keyboard/Vendorのcallback設定とNKRO設定はesp_err_t・停止中に統一し、protocol値はatomicにした。

新規`validation/s3/hid_report_state`は2 IDの状態、初期値、reset、Feature更新と
200回の並行読出しを確認する。20/100 ms指定の観測平均は20.873/108.002 msで、
複数packetのwire時間も含む。ID別/全体の停止も確認した。
`hid-report-state.xml`は2 passed、127.44秒、relative/Mouseを含む`hid-state-relative.xml`は3 passed、123.33秒。
確保失敗は200取得点まで拡大し、Feature setterに追加確保がないことも確認した。
3最小build/nmで未使用HID・取消し・TinyUSB CDC/Vendor/HIDの不在も維持した。
詳細とRAM増加・移行は[V3_HID_API.ja.md](V3_HID_API.ja.md)。

最終の標準`hid-report-idle-all.xml`は**10 passed、0 skipped、0 unfinished、520.13秒**。
200取得点の失敗/回復、Feature setterの追加確保なし、短いboot定義の拒否、停止中設定も含む。
全caseで両board STOPPED、panicなし。serial原本は`/tmp/pytest-embedded/2026-10-26_13-02-08-326097/`、
JUnit/transcript/serialコピーは`validation/.pytest-results/hid-report-idle-all*`。
TinyUSB tree照合は51 file / 15 sourcesで一致し、旧test/sampleはmainと同一のまま。


## MIDI classの独自化と最小event queue

`EspUsbDeviceMidi.cpp`へ分離し、CFG_TUD_MIDI=0、単一global instanceとtud_midi_*呼出しを撤去した。
共通bulk上で4 byte eventを保ち、runtime cable/queue/transfer設定、外部ring、typed callback、片方向を扱う。
既存helperは維持する。無効cable数と予約CINを拒否する変更の利点/移行は[V3_MIDI_API.ja.md](V3_MIDI_API.ja.md)へ記録した。

bulkのRXはDMA packet内の未読部分を保持するため、ring容量1でも完全packetを失わず順次読める。
byte streamにも適用し、available分を一度に読む場合も残りを取り出す。
MIDIのTX検証はbulk lock内で行い、送信中endと終了後の停止中設定を分離する。
未登録CDC/Vendor/MIDIの手動beginもInvalidStateで拒否し、既存通信を壊さない。

標準`--combo all`の`runtime-midi-all.xml`は**11 passed、0 skipped、0 unfinished、586.92秒**。
その後のlock整理、非対称cable/全helper/並行end試験追加後、関連3件の
`midi-stream-final.xml`は**3 passed、0 skipped、0 unfinished、192.03秒**。
失敗注入は313取得点の失敗/回復を確認。各実行の全caseで両board STOPPED、panicなし。
serial原本はそれぞれ`/tmp/pytest-embedded/2026-10-26_13-53-51-584234/`と
`/tmp/pytest-embedded/2026-10-26_14-12-20-431884/`、コピーとtranscript/JUnitは`.pytest-results/`。

plugin 1.10.0のcomboガイドも再確認した。結線済み組の列挙と`--combo all`を継続し、
conftestや独自hookは追加していない。既存test/sampleは設計対象に含めない。
残るAudio/MSC/NCM/Video/DFU等のclass、共通EP0/core/DCDのTinyUSB依存は別作業である。

Device-only/Vendor-copy/CDC-copy/MIDI-packetの4最小buildとnm確認も成功した。
MIDI未使用ならEspUsbDeviceMidiはリンクされず、全4件ともTinyUSB CDC/Vendor/HID/MIDIと未使用取消しを含まない。
TinyUSB tree照合は51 file / 15 compiled sourcesで一致。mainはv3の祖先で、旧tests/examplesへの差分はない。


## 未完IN中のHID protocol切替

2026-10-26。SET_PROTOCOLのSETUP時に未完の旧形式INを停止し、旧完了eventのFIFO fenceが通るまで
明示/idle送信を保留する。endpointのDATA PIDは維持し、既存idle jobでqueue満杯の予約を再試行する。
boot-capableな番号なしInputは現在の形式の正確な長さを検証し、形式選択と送信の競合を不一致のまま受理しない。
通常helperの呼出しは維持する。APIの利点/不利益はV3_API_DECISIONS.ja.mdへ記録した。

新規`validation/s3/hid_protocol`は公開IDF Host APIでINを手動投稿し、旧reportをDevice DMAに保留する。
Keyboard29↔8 byte、Mouse4↔3 byteを各4回切替。二つ目の送信の拒否、切替後30 msの未受信、
新markerの全byteと長さ、誤形式のInvalidSizeを実通信で確認した。
初回はCoreで有効な列挙filterのcallback省略によりHost未準備で失敗した。
診断のINSTALL/CLIENT成功・OPEN未発生とIDFのenum.cを照合し、標準enum_filter_cbで対象VID/PIDを許可して修正した。
この試験だけmanual Hostを使い、Host libraryへのpatchやpytestの独自fixture/hookは追加していない。

`hid-protocol-related.xml`は**4 passed、0 skipped、0 unfinished、171.86秒**。
新規probe、既存HID transport/report state、313取得点の確保失敗/回復を含み、全caseで両board STOPPED、panicなし。
serial原本は`/tmp/pytest-embedded/2026-10-26_14-27-21-836610/`、JUnit/transcript/serialコピーは`.pytest-results/hid-protocol-related*`。
通常の`--combo all`の対象は12件になったが、今回実行したのは関連4件で、全12件実行済みとはしない。
人工的な旧完了eventとqueue満杯の組合せ、protocol切替とreset/endの同時実行、他SoC/OSは未検証。
TinyUSB treeは51 file / 15 compiled sourcesで一致、旧tests/examplesはmainと同一。


## MSC classの独自化

2026-10-27。MSCをEspUsbDeviceMsc.cppへ分離し、固定4096 byte bufferとglobal instance、
TinyUSB MSC class/callbackを撤去した。runtime staging容量、外部DMA buffer、複数Function、
write callbackを省略するread-only、停止中setterを実装した。
関連4試験はmsc-driver-related.xmlで4 passed、0 skipped、0 unfinished、148.36秒。
BOT13ケース、不正CBW6条件、正確なCSW/Senseと復帰、独立2 MSC、通常API、
362取得点の失敗/回復と未登録Functionの拒否を含む。各case両board STOPPED、panicなし。
API・移行・Host側PID同期の制約・原本は[V3_MSC_API.ja.md](V3_MSC_API.ja.md)を参照。
512 byte以外のsector境界、最小build/nmと全14件は下記で補強した。
残るAudio/NCM/Video/DFU等、共通EP0/core/DCDの置換は未完である。


MSCの最終OUTをMPS単位でarmし、処理するbyte数をSCSI payloadまでに制限した。
511/512 byte sectorのBOT13ケースと通常APIはmsc-packet-boundaries.xmlで2 passed（93.48秒）。
5最小build/nmも成功し、未使用MSCとTinyUSB MSCの不在を確認した。
隣接sectorのguardを加えたruntime-msc-all.xmlは**14 passed、0 skipped、0 unfinished、772.40秒**。
362取得点の失敗/回復を含み、全caseで両board STOPPED、panicなし。
serial原本は/tmp/pytest-embedded/2026-10-26_17-57-02-044220/、コピーとJUnit/transcriptはvalidation/.pytest-results/runtime-msc-all*。
5最小build/nmも全件成功し、旧tests/examplesはmainと同一、TinyUSB tree照合は51 file / 15 compiled sourcesで一致。

次の実装はAudioの最小周期経路とruntime容量・callback所有権を調べ、残るclassを独自化すること。
MSCのOS/media試験、人工的な旧完了eventとqueue満杯の組合せ、reset/endの同時条件は未検証として残す。
共通EP0/core/DCDとP4/S2/S31の実機対応は未完であり、最終目標の達成とはしない。


## Audio eventのruntime容量と検証の入口

2026-10-27。EspUsbAudioConfig.eventCapacityを追加し、begin時に実数分だけ確保、0でevent配列を省略する。
queueをmulti-producer対応にし、稼働中の構成変更と未登録AudioのFIFO操作を拒否する。
Function破棄はUSB停止後にevent領域を解放する。PCMのlegacy driver/FIFO/globalはまだ残る。
新規audio_eventsとallocation_failureのaudio-events-related.xmlは2 passed、0 skipped、0 unfinished、77.39秒。
容量0/1/8/17件の実USB Mute、200件の並列producer、metadata反復、稼働中破棄、
413取得点の失敗/回復を含み、全case両board STOPPED、panicなし。
API/利点・不利益/原本は[V3_AUDIO_API.ja.md](V3_AUDIO_API.ja.md)を参照。

次は独自Audio transportへ進む。FS/HSのpacketサイズをdescriptorから求め、選択方向だけのring/DMAを確保する。
UAC1/UAC2 control、feedback、captureの端数rate、alt設定変更/reset/endと旧完了eventを揃えて検証する。
現状はformat/control stateの固定配列と単一形式・single global制約があり、今回のevent変更だけでは解消していない。
全体収集は15件になった。直近の全体成功はMSC時点の14件（772.40秒）で、今回は関連2件を実行した。


## Audio classの独自化

2026-10-27。TinyUSB Audio class/FIFOとsingle globalを撤去した。選択方向とformatの最大packetに応じたDMA、
方向別runtime ring（frame数・外部buffer・容量0）、UAC1/UAC2 control、capture端数rateと無音補完、
USB由来の公称feedback、alt変更時のabort/FIFO fenceを実装した。周期処理はworkerで実測し、ISR経路は追加していない。
Captureは部分書込みの戻り値を使うbackpressureへ変更。clearBufferはarm済みpacketを取り消さない。
S3 Hostは公開FIFO設定{64,32,64}を使用。EspUsbHost 2.10.1の存在しないOutput方向への統計/Running queryを避ける。
Device仕様をHost不具合に合わせて変えず、Host libraryも変更していない。


独自PCM driverの関連試験audio-pcm-related.xmlは**3 passed、0 skipped、0 unfinished、145.99秒**。
PCM・event制御・438取得点の失敗/回復を含み、両board STOPPED、panicなし。
serial原本は/tmp/pytest-embedded/2026-10-26_18-51-19-326855/、コピーとJUnit/transcriptはvalidation/.pytest-results/audio-pcm-related*。
Audioのみ/Deviceのみの最小buildとnmも成功し、旧TinyUSB Audioと未使用独自Audioの不在を確認した。
通常収集は16件。この関連3件の後に実行した全16件の結果は下記を参照。

次は全16件の一括確認と、残るclass/core/DCDの独自化へ進む。Audioの複数format、固定model/control配列、
外部codec clock feedback、borrowed/callback PCM API、他OS/SoC/HSは未完。詳細はV3_AUDIO_API.ja.md。


## Audio置換後の全体確認

2026-10-27。標準の`--combo all`で**16 passed、0 skipped、0 unfinished、823.56秒**。
全16 caseで両board STOPPED、32本のserialにpanicなし。438取得点の確保失敗/回復、
UAC1/UAC2 PCMとevent、CDC/Vendor/HID/MIDI/MSC、descriptor/registryと取消し・転送境界を含む。
serial原本は`/tmp/pytest-embedded/2026-10-26_18-54-11-801467/`、コピーとJUnit/transcriptは`validation/.pytest-results/runtime-audio-all*`。
Audioのみ/Deviceのみの最小build/nmも成功。TinyUSB treeは51 file / 15 compiled sourcesで一致し、
旧tests/examplesはmainと同一、mainはv3の祖先。残るclass/core/DCDと他SoC/HSの置換・検証は未完である。


## DFU classの独自化

2026-10-27。TinyUSB DFU Runtime/Download class、global stateと固定1024 byte領域を撤去した。
EspUsbDfuConfigで64..65535 byteと外部buffer、poll timeout/manifestationを指定する。
Runtimeはblock領域を持たず、Functionごとに状態・backendを分ける。既定OTAに加えて同期backendを追加した。
onErrorはEspUsbDfuStatusへ、transferSizeはinstanceへ変更し、稼働中setterを拒否する。
API判断と実測はV3_DFU_API.ja.mdを参照。


## S3–S3の実測結果

`dfu-runtime-related.xml`は**2 passed、0 skipped、0 unfinished、74.27秒**。
64/129/4097/65535 byte、内外buffer、独立2 Download + Runtime、GETSTATUSとblock順序、
不正要求のSTALL/CLRSTATUS、write/manifest失敗、ABORTとend時の一度だけの回収、
manifestation tolerantとreset待ち、DETACHのtimeout復帰を確認した。metadata反復のheapも回復した。
502取得点の確保失敗/回復は338520→338520 byte、両case両board STOPPED、panicなし。
serial原本は`/tmp/pytest-embedded/2026-10-26_19-25-20-261549/`、コピーとJUnit/transcriptは`validation/.pytest-results/dfu-runtime-related*`。
DFUのみ/Deviceのみの最小build/nmで、旧TinyUSB DFUと未使用独自DFUの不在を確認した。
試験は公開IDF Host APIのRAM backendで行い、conftestや独自pytest hookを使わない。
Coreのenum filterが有効なため、Hostは対象VID/PIDのfilterを明示する。
通常収集は17件。関連2件の後に行った全17件の結果は下記を参照。

残るclassはNCM/Video。共通EP0/core/DCDは暫定TinyUSBのままで、全置換の完了ではない。
実OTA/再起動とdfu-util、block周回、EP0途中切断・実書込み中reset、他SoC/HSは未検証。
Runtimeのみでも共通driverのDownload/OTA codeがリンクされ得る点は、後続の費用整理に残す。


## DFU置換後の全体確認

2026-10-27。標準`--combo all`は**17 passed、0 skipped、0 unfinished、841.47秒**。
全17 caseで両board STOPPED、34本のserialにpanicなし。502取得点の確保失敗/回復、
DFUの最大block/複数interface/manifestationと、従来のAudio/CDC/Vendor/HID/MIDI/MSC、
descriptor/registryと取消し・転送境界を含む。
serial原本は`/tmp/pytest-embedded/2026-10-26_19-27-27-183733/`、コピーとJUnit/transcriptは`validation/.pytest-results/runtime-dfu-all*`。
DFUのみ/Deviceのみの最小build/nmも成功。TinyUSB treeは51 file / 15 compiled sourcesで一致、
旧tests/examplesはmainと同一で、mainはv3の祖先。実OTAの適用・再起動と他SoC/HSは未検証。

次のclass置換はNCM。src/class/net/ncm_device.cの固定NTB poolだけでなく、EspUsbDevice.cppの
g_activeNet・tud_network_mac_address/g_netMacString・NET_TX_SLOTSとFreeRTOS queue、
MAC文字列の固定indexとbeginNetworkのnetif glueもFunctionごとに整理する。
Hostにはnetwork raw-frame APIがあるため、S3対向でEthernet frameとNTB control要求から検証する。
Videoの後には共通EP0/core/DCDの独自化が残る。DFUのRuntimeだけのflash費用分離も未完。


## Function固有の補助文字列

2026-10-27。NCMのMAC文字列をFunction単位へ移す前提として、EspUsbDeviceClassに
auxiliaryStringCount/auxiliaryString/assignAuxiliaryStringIdsを追加した。
Function名の後へ連続したindexを割り当て、既存のexact UTF-16 snapshotへ一緒に保存する。
使用しないFunctionの追加領域はなく、既存の名前indexと251 Functionの上限を保つ。
countとtextはbeginの処理中に安定していることがwriter契約であり、宣言した文字列のnullptrは拒否する。
稼働中のUSB応答はsnapshotなので元配列の変更に依存しない。

`function-auxiliary-strings.xml`は**3 passed、0 skipped、0 unfinished、138.04秒**。
複数Functionの別領域、空の名前、index255、超過/SIZE_MAX/null/不正UTF-8からの回復、
実USB上の名前5と補助6/7・配列変更後の応答、既存registryと502取得点の失敗/回復を確認した。
両board STOPPED、panicなし。serial原本は`/tmp/pytest-embedded/2026-10-26_19-51-05-602028/`、コピーとJUnit/transcriptは
`validation/.pytest-results/function-auxiliary-strings*`。
通常収集は17件のまま。今回の関連3件は、DFU段階の全17件成功とは別の結果である。
NCMのclass/NTB pool/MAC globalはまだlegacyのままで、この変更だけで複数NCM通信に対応したとはしない。


## NCM classの独自化

2026-10-27。固定NTB poolとMAC、永続frame queue、network worker専用drainを撤去した。
EspUsbDeviceNet.cppの独自driverでFunctionごとのNTBとslot metadata、mandatory通知を確保する。
外部DMA領域/方向0/Host容量再交渉/alt時abortとFIFO fence、SET_INTERFACEだけのDATA PID初期化を実装した。
補助文字列にMACを割当て、既定MACもFunctionごとに分ける。
IP層はEspUsbDeviceNetif.cppへ分離し、beginNetworkを呼んだ場合だけglueを導入する。
endNetworkとDevice.endでnetifとkey/IP stateを破棄する。通常rawではIP処理をリンクしない。
APIと移行・残る検証はV3_NCM_API.ja.mdを参照。


## S3–S3の実測

`ncm-driver-related.xml`は**3 passed、0 skipped、0 unfinished、143.77秒**。
NTB容量2048/3200/8192、1/2/3 buffer、外部pool、14..1514 byte frame、ZLP、まとめ送り、
容量再交渉と不正要求後の復帰、heap回復と605取得点の確保失敗/回復を含む。
手動Hostで22種類の不正NTBを捨てて直後の正常frameを返し、block長0、末尾NDP、逆順datagram、
8128 byteのhardware armを跨いだpayload、独立2 NCMとMAC、12回のalt切替とPID初期化を確認した。
serial原本は`/tmp/pytest-embedded/2026-10-26_20-17-53-324449/`。

`ncm-ip-related.xml`は**2 passed、0 skipped、0 unfinished、154.11秒**。
3構成ともDHCPで192.168.7.2を取得しTCPの8193 byteが一致、IP停止後にraw通信へ戻れた。
2 NCMの別IP/keyでの同時作成・片方停止・再作成も成功した。
serial原本は`/tmp/pytest-embedded/2026-10-27_00-57-48-429795/`。
両実行とも各case両board STOPPED、panicなし。コピーとJUnit/transcriptは
`validation/.pytest-results/ncm-driver-related*`と`ncm-ip-related*`。
Netのみ/Deviceのみの最小build/nmも成功し、旧TinyUSB NCMと未使用NCM/IP glueの不在を確認した。
通常収集は19件。関連試験の後に行った一括試験の結果は下記を参照。

次は全19件の一括確認。残るlegacy classはVideoで、共通EP0/core/DCDもまだTinyUSB。
NCMはNTB16/CRCなし、PC/OS・他SoC/HSとroute optionの詳細は未検証。


## NCM置換後の全体確認

2026-10-27。標準`--combo all`は**19 passed、0 skipped、0 unfinished、1080.50秒**。
全19 caseで両board STOPPED、38本のserialにpanicなし。NCMのraw/control/2 interface、
DHCP/TCPとIP回収、605取得点の確保失敗/回復、および既存の全USB経路を含む。
serial原本は`/tmp/pytest-embedded/2026-10-27_01-02-46-482679/`、コピーとJUnit/transcriptは`validation/.pytest-results/runtime-ncm-all*`。
NCMのみ/Deviceのみの最小build/nmも成功し、旧TinyUSB NCMと未使用NCM/IP glueの不在を確認した。
TinyUSB treeは51 file / 15 compiled sourcesで一致。旧tests/examplesはmainと同一で、mainはv3の祖先。
残るlegacy classはVideo。共通EP0/core/DCDと他SoC/HSの置換・実機検証は未完。


## Video独自化前の設定・寿命の修正

2026-10-27。Videoの稼働中破棄はDeviceを停止し、USB worker/DMAが止まってから状態を解放する。
prepareToStopで新規frameを拒否するが、稼働中フラグは終了処理まで保持して設定変更を防ぐ。
未登録/二重begin/別runtimeの稼働中beginはInvalidStateとし、startErrorで理由を返す。
raw descriptor writerも稼働中の書換えを拒否する。

format/size/fps/最大frameを候補全体で検証し、失敗時に旧値を保持する。
未知format、YUY2の奇数幅・実サイズと異なる固定frame、32 bit bit rateのoverflowを拒否する。
MJPEGの奇数サイズと1..255 fpsを受け付け、transportと関係しない120 fps制限を撤去した。
開始時にも構成を再検証する。最大4096の寸法制約とVideoの一形式という構成は現状維持。

`video-lifecycle-related.xml`は**2 passed、0 skipped、0 unfinished、344.96秒**。
metadataの反復とheap回復、257/511/512/1021 byteの実ISO frame一致と完了callback、
未登録Videoの生成/破棄後の正常通信、64 KiB送信中のFunction破棄とbuffer解放、既存descriptor試験を含む。
Hostの公開FIFO設定はRX160/NPTX16/PTX16 lines。両case両board STOPPED、panicなし。
serial原本は`/tmp/pytest-embedded/2026-10-27_01-30-16-696638/`、コピーとJUnit/transcriptは`validation/.pytest-results/video-lifecycle-related*`。
通常収集は20件。全体の直近成功はNCM時点の19件で、今回の関連2件と区別する。

Videoのclass/FIFO/global ownerはまだTinyUSBのまま。payload容量/ISO・BulkのAPI化と独自driver、
Probe/Commit検証、reset/alt/end時のframe所有権通知、callback contextの統一は次の実装対象。
今回の実転送はsynthetic MJPEG payloadのUSB転送検証であり、JPEG decode・PC/OS・他SoC/HSの検証ではない。

## Video独自化の引き渡し

2026-10-27の独自Video driver最終関連試験: `video-driver-cancel.xml`、
**7 passed、0 skipped、0 unfinished、212.48秒**。
ISO/Bulk各64/65/512-byte payloadの6構成を同一binaryの実行時APIで選び、
各構成で4 frameの全byte一致、成功結果のpointer/id/byte数、64 KiB frameの取消しと古いticket拒否、
取消し直後の再投稿、送信中破棄によるStopped一度だけの返却を確認した。
65-byte構成は64-byte alignment・128-byte以上の外部DMA領域を利用する。
既存の独立descriptor probeを含む。14 serial log全てSTOPPED、panicなし。
serial原本は`/tmp/pytest-embedded/2026-10-27_01-53-40-022879/`、コピー/JUnit/transcriptは`validation/.pytest-results/video-driver-cancel*`。
VideoOnlyとDeviceOnlyの最小buildも成功し、nmでTinyUSB Video symbol不在、
DeviceOnlyには独自Video実装もリンクされないことを確認した（`*-video-nm.txt`）。
通常収集は25件。全体の直近成功はNCM時点の19件で、今回の関連7件と区別する。
Videoの確保失敗・複数Function・不正control・cancel/reset/recommit競合は後続の試験対象。
共通EP0/core/DCDと他SoC/HSは未完了であり、v3全体の完成ではない。

次はVideoの確保失敗・複数instance・EP0/取消し競合の追加検証を行い、標準combo allで全件を確認する。
その後、共通EP0/core/DCDの置換へ進む。新APIを旧test/exampleに合わせて戻さない。

## Videoの確保失敗・複数Function・制御要求

2026-10-27。`video-driver-extended.xml`は**9 passed、0 skipped、0 unfinished、175.58秒**。
Video ISO/Bulkの内部/外部staging、2 Function、65535-byte stagingを含む670取得点を
一つずつ失敗させ、全点でNoMem、全資源回収、再開始とheap回復を確認した。
`video_control`は2 Functionを同時に列挙し、MJPEGの65-byte payloadとYUY2の128-byte payloadを実通信した。
26/34/48-byte Probe/Commit、GET_LEN/INFO/MIN/MAX/RES/DEF、22件の不正要求のSTALLと復帰、
不正frame/NULL/busy/別Functionのticket拒否、片方のalt/recommit/cancel中に他方のframeを保持・完了できることを確認。
62-byte frameのBulk ZLP終端、両方の送信中endとStopped一度だけの返却、外部DMA領域前後のcanaryも含む。
ProbeのGET_MIN payloadを3、GET_RESを1に直し、受け付ける範囲と一致させた。
この関連9件には既存のISO/Bulk各3構成と独立descriptor試験も含む。
18 serial log全てSTOPPED、panicなし。原本は`/tmp/pytest-embedded/2026-10-27_02-07-12-940635/`、
コピー/JUnit/transcriptは`validation/.pytest-results/video-driver-extended*`。
通常収集26件。全体の直近成功19件とは区別する。
実USB bus resetと取消し/recommitの競合全順序、PC/OS、HS/他SoCは引き続き未確認。

次は無効化済みのlegacy classソースを除去し、標準combo allで26件を確認する。

## Videoとlegacy classソース除去後の全体試験

2026-10-27。TinyUSBの無効化済みclass実装10本と関連headerを削除した。
残るvendor選択は27 file / 5 compiled sources。共通EP0/core/DCDとAudio/CDC/HID/MIDIのprotocol定義は残る。
検証scriptと更新scriptのmanifestも揃え、削除したNCMのlocal patchを現manifestから外した。
出所とMIT表示は維持する。既に無効だったソースの削除なのでFlash/速度の改善を主張しない。

標準`--combo all`の`runtime-video-all.xml`は**26 passed、0 skipped、0 unfinished、1703.91秒**。
670取得点の確保失敗/回復、全独自classの既存経路、複数Video制御、ISO/Bulk各3構成を一括実行した。
52 serial log全てSTOPPED、panicなし。Video deviceのnmでもlegacy class driver symbol不在を確認した。
serial原本は`/tmp/pytest-embedded/2026-10-27_02-12-17-085198/`、コピー/JUnit/transcriptは
`validation/.pytest-results/runtime-video-all*`。symbolは`video-control-cleanup-nm.txt`。
Core 3.3.12 / Host 2.10.1 / plugin 1.10.0、S3–S3のみ。conftestや独自pytest hookは使わない。

次は共通EP0/core/DCDを独自実装へ置き換える。reset/新SETUP/STALL/status、
標準要求、event順序とDMA停止、suspend/remote wakeupを分けて検証する。
他SoC/HS、PC/OS、長時間試験、出荷exampleと最終API整理は未完了。

## 共通coreの関連検証

2026-10-27。`core-controls-related.xml`は**5 passed、0 skipped、0 unfinished、323.22秒**。
control_core、712取得点の確保失敗、cancel_fence、cancel_lifecycle、video_controlを実行した。
core用private取得点はbegin時の一つで、全失敗点で回復・終了時の未回収0とheap回復を確認した。
control_coreはキュー深さ1/4/16/31で、空・短い・64-byte境界を跨ぐIN/OUT、19件の不正標準要求、
configuration解除と同値再選択、借用返却、意図的なqueue過負荷と自動再列挙後の正常通信を確認した。
各構成でVendor control 20回（IN11、OUT7、no-data2）、借用TX成功2・reset返却3。
FreeRTOSの自己削除taskのTCBはidle taskが回収するため、heap比較は各終了後10 ms待って行う。
10 serial log全てSTOPPED、panicなし。原本`/tmp/pytest-embedded/2026-10-27_03-16-42-737527/`。
コピー/JUnit/transcriptは`validation/.pytest-results/core-controls-related*`。

`core-standard-final.xml`は1件成功（81.61秒）。GET_DESCRIPTORの長さ0/1/8/18/64も追加した。
続いて未使用bootstrap/FIFO sourceを除去した`core-no-legacy-helpers.xml`も1件成功（52.57秒）。
各2 serial logで停止とpanicなしを確認し、nmで独自coreの存在と旧core state/FIFO symbol不在を確認した。
現在のvendor選択は24 file / 2 compiled sourcesで、コンパイルするTinyUSB sourceはDWC2関連だけ。
ABI/header依存を含む全撤去はまだ完了していない。

次は全27件を一括検証し、その後、SET_INTERFACE/HALTの転送中所有権、EP0世代境界・suspend等を補強してDWC2の独自化へ進む。

## 独自coreでの全体試験

2026-10-27、実装`ce6a444`。標準`--combo all`の`runtime-core-all.xml`は
**27 passed、0 skipped、0 unfinished、1743.12秒**。
712取得点の確保失敗/回復、独立coreの4キュー深さ、過負荷と自動再列挙、各既存クラスとVideoの全経路を一括実行した。
54 serial log全てSTOPPED、panicなし。
原本は`/tmp/pytest-embedded/2026-10-27_03-35-23-682254/`、コピー/JUnit/transcriptは
`validation/.pytest-results/runtime-core-all*`。Core 3.3.12 / Host 2.10.1 / plugin 1.10.0、S3–S3のみ。
TinyUSB manifestは24 file / 2 compiled sourcesで照合成功し、旧test/sampleは変更していない。

この成功は全標準要求・全競合順序や他SoC/HSの検証完了ではない。
転送中のSET_INTERFACEとHALT/CLEAR_FEATUREにおけるクラス別PID・所有権回復、
新SETUPと旧status/dataの到着順、物理short/overrun、suspend/remote wakeupは引き続き対象。
残るDWC2・ABI/header依存、公開APIの最終整理、出荷exampleとOS互換性も未完了。
