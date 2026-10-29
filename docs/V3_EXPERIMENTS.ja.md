# v3 実験台帳

2026-10-26。実施済みの準備試験は[V3_HANDOFF.ja.md](V3_HANDOFF.ja.md)に記録する。
本書E1以降は新実装を使って判断する実験仕様である。実施済み・未実施は現在の引き渡しと
開発計画の照合表、各API/validationの記録を参照し、本書だけから合否を推定しない。
v2との網羅的な速度比較は不要。新実装の設計判断に必要な小さい比較だけ行う。
数値は初回の試験条件であり、根拠があれば測定後に変えて判断理由を残す。

## 記録の共通形

各実験で、Device/Host/Core/pluginのcommitまたはversion、dirty差分、profile、board識別、
結線と実効speed、設定、コマンド、結果、ログへのpath、採用/棄却/未判定を記録する。
データは連番・長さ・CRCで欠落/重複/破損を検出する。必要な時だけ時間とheap/stackを記録する。
speedをprofile名から推定しない。FS接続の結果をHSと書かない。
実験用観測は有効化しなければ転送経路へbuffer・lock・ログ出力を追加しない。

次の形式でこの台帳に追記する:

```text
E番号 / 日付 / revisionとdirty状態:
問い・比較した案:
profile / 実機 / negotiated speed:
入力条件・実行コマンド:
観測・artifact:
判定・採用値・理由:
残った条件 / 次の実験:
```

## E0: 独立したS3–S3環境

- `validation/`でconftestなし、旧test/helperなし、新規Host/Device sketchのみを収集する。
- S3–S3のVendor往復を1/63/64/65/127/128/129 byteで確認する。開始・停止はtest本文で明示する。
- plugin 1.10.0の標準`arduino_combos`・port・lock・dut/peersを使用し、`--combo all`で実行する。
  現在はS3–S3一組だけ。通信assertionをretry/skipで隠さない。
- 初期は15ケースだった。現在の収集数・成否は各実行のJUnitと引き渡しで確認する。終了値0だけでは全件skipを区別できない。
- pluginは組合せ単位のログ・結果とcleanup失敗後の中断を提供する。
  必須case監査・serial監査など不足が必要になったらpluginへの追加候補として整理する。
  独自conftestを別のファイルへ移して使い続けない。
- 新APIの優先実験は[V3_API_DECISIONS.ja.md](V3_API_DECISIONS.ja.md)の短いwriteと容量/mode変更。
  E1/E2の基盤は、その実装に必要な分だけ作る。

## E1: 未使用費用、registry、確保失敗

**問い:** Function表を分離すれば必要なクラスだけがリンクされ、資源が設定に比例するか。

1. 最初はS3同一profileで「ライブラリなし」「includeのみ」「Device宣言のみ」「Vendor」を
   新規sketchとしてbuildし、map/data/bss/Flashを保存する。後続クラスの実装時に
   「Keyboard」「CDC」「Keyboard+CDC」「CDC二つ」を追加し、先に全構成を要求しない。
2. function/helper別のsymbolの参照元を追う。includeだけでtask/PHYを起動しない。
   CDCのみでAudio/NCM/MSC/SD/OTAの実装・bufferが残れば参照を切る。
3. CDC容量128/512/2048、1個/2個でbegin前後・end後のheapとtask数を記録。
   初回lazy初期化の影響を分離し、100回begin/endで単調に減る資源がないことを確認。
4. allocator shimでbeginの各確保箇所を順に失敗させる。列挙を始めずStoppedへ戻り、
   次回の正常beginが成功することを確認。ユーザーbufferでは所有外領域をfreeしない。

**判定:** 一括最大数確保や全クラス参照があれば構造を修正する。共通費用とinstance費用を説明できれば、
絶対byte数の目標を先に置かない。仮TinyUSB adapterの残存費用は別記し、最終合格に数えない。
borrowed bufferでheapを減らせても基本例が複雑になるなら、自動確保を標準、借用を任意とする。

## E2: 完了通知、競合、停止

**問い:** USB worker上で即時通知する方式で十分か。遅延通知/pollingを標準にする利益があるか。

1. fake DCDでsubmit→complete、submit→cancel、completeとcancelの両順序、reset、
   detach/end、queue満杯、新世代へ古いeventが到着する場合を列挙する。
2. 受理数=終了通知数、同じticketは一回、拒否ticketの通知ゼロ、DMA停止前返却ゼロをassert。
   callbackから次のsubmitを行い、lock再入や古いbuffer使用を検出する。
3. S3 Vendorでbuffer 2個/queue 1,2,4を比較。producerを別taskへ置き、1000転送中に
   100回reset/取消し/停止を挿入。P4では別core producerでも確認。
4. アプリ側に0/1/5msの処理遅延を入れ、直接callbackはsignalだけに留める。
   遅延通知案ではpoll間隔を同じ値にし、queue満杯と回収停止を観測する。
   採用候補ごとに空転task、stack、最大保持buffer数、転送停止・周期転送への影響を記録。

**判定:** どの案も回収契約を守る。queue overflowを落として解決しない。
直接通知で機能を満たすなら複雑な二段queueを必須にしない。
polling案を採る場合はアプリがpollしない時とend時の返却を先に解決する。
CH32の集約通知を名前だけ合わせて移植しない。

## E3: byte stream、packet終端、direct RX/TX

**問い:** FIFO/borrowedの切替とshort/ZLPをAPIで表せるか。OEP以外の要求応答や連続streamも成立するか。

- S3 FSから開始し、P4 HSで再実行。MPSを実効descriptorから得る。
- 長さ0,1,MPS-1,MPS,MPS+1,2×MPS,4096,65535,65536を入力。
  最大値超過は明示拒否でもよい。内部16bit truncationや先頭だけ成功は不可。
- Host read長を送信長と同じ/より大きい場合で比較し、Continue/EndWithShortPacketを試す。
  倍数長で次のsubmitがZLPと競合しないこと、余計なZLPが次readへ漏れないことを確認。
- 受信はFIFOと投稿bufferを比較。Host OUTのshort/ZLP、buffer満杯、未投稿を試す。
  bulkのbackpressureと再開、actualBytes、bufferの一度だけの返却を確認。
- FIFO→borrowed→FIFOをend/configure/beginで切替。build_opt.hもclean rebuildも不要であることを確認。
- Hostが読まない/途中で止まる/切断する条件でもcopy writeが受理した量と、borrowed転送の終了結果を区別する。

**判定:** protocolが要求する終端を明示APIで満たす。OEPの「末尾1byteを別送する」等の回避策を仕様にしない。
通常writeのflush規則は短い要求応答と連続stream双方のログで選ぶ。
strict directを成立させられないメモリはコピーAPIへ明示的に誘導する。

## E4: DMA、cache、transfer分割（S3でAPI成立後）

**問い:** 利用者がcacheを意識せず安全にzero-copyを選択できるか。

1. P4 loopbackでHS controllerをFS linkで駆動し、S3と異なるcache実装を先に確認。
   実際のHS linkでは別途P4 DeviceをPC HS Hostまたは一時P4 Hostへ接続する。
2. DMA allocatorのbuffer、意図的な非alignment、長さ非倍数、隣接cache line canary、
   不適合memoryを試す。非DMA/PSRAMを直接許す根拠がなければ拒否を合格とする。
3. TX/RXそれぞれ、cache clean/invalidate後の隣接データをcanaryで検査。
   別core producer、転送中cancel/reset/endの直後にbufferを再利用して破損がないか確認。
4. hardware packet count/transfer byte fieldの境界前後を計算し、上限、上限+1、分割後の末尾を試す。
   生registerの上限をsize_t APIで隠してoverflowしない。

**判定:** buffer開始alignmentだけでなく領域末尾のline共有も解決する。
借用条件・allocator・maxTransferBytesを実測で確定。分割が必要ならticketは全体で一つ、
中断時のactualBytesは保守的に定義する。先にFSで成功してもHS対応を宣言しない。

## E5: Audio/Videoで共通設計を検証

**問い:** bulk向けqueue/通知/停止の設計が周期転送を阻害しないか。

- S3で48kHz stereo 16bitのplayback/capture/duplexを最小構成として、packet保持2/4/8を比較。
  44.1kHzも加え、packetごとのsample数が固定ではない経路を確認する。
- callbackはPCM境界だけとし、I2S/codecを入れず連番PCMを10分送受信。
  underrun/overrun、遅延、frame欠落、feedbackの推移、heap変動を記録する。
- 同時CDC traffic、alt0↔streaming、rate変更、suspend/reset/endを反復する。
  alt0で未使用転送bufferを何まで保持するか、復帰遅延とRAMを比較して決める。
- P4 HSでFSでは成立しない高rate構成を追加し、FS fallback時には対応formatだけを公開するか、
  要求した構成を拒否するかを明記する。黙って別rateへ変えない。
- Videoはbulk/iso切替、payload/header容量、frame終端、host停止を同じ資源管理で試す。

**判定:** 初期buffer値は欠落・メモリ・必要遅延から選ぶ。isoは損失時にbulk同様の再送成功を捏造しない。
Audioのfeedbackなど必須protocolを高速化のため削除しない。周期転送が成立してから全classへ展開する。

## E6: controller能力とEP0/DCDの置換

- SoC/portごとにendpoint番号、IN数、FIFO words、DMA予約領域をソースと能力registerで照合。
  HID/CDC複合を増やし、限界内で列挙・全endpoint転送、限界+1でbeginが明示拒否することを確認。
  S2/S3の現validatorとDWC2 tableの差はここで解決し、測定前に上限を広げない。
- fake DCDでSETUP/data/statusと割込み順序を試す。SET_ADDRESS適用、長さ0/部分descriptor、
  malformed request、SET_CONFIGURATION 0/1、SET_INTERFACE、STALL/clear、resetを網羅。
- 実機ではHost control転送を使って同じ要求を再現。遅延reply中の新SETUPとresetを加える。
  DeviceとEspUsbHost双方が同じ誤解をしていないか、PC Linuxのdescriptor/control取得でも確認。
- suspend/resume/remote wakeupは許可状態とtimingをUSB仕様・対象SoC資料で照合し、
  OSで観測してから対応済みにする。callbackやmock成功だけで判断しない。
- 置換後のmapとsymbolの参照元を監査。ライブラリ自身とCore同梱TinyUSBの両方を対象にする。
  残したライセンス表示は依存残存と混同しない。

## E7: クラスと設定の移行漏れを防ぐ

| 現行設定・制約 | v3での置き場所 | 最小の確認 |
| --- | --- | --- |
| `CFG_TUD_*`クラス有効/個数、MAX_CLASSES | 登録Functionのみ、個数は実資源制限 | HIDのみ、CDC二つ、複合、上限拒否 |
| CDC/MIDI/Vendor RX/TX BUFSIZE、TXRX_BUFFERED | instance config、FIFO/borrowed mode | 容量を変更して再開始、別instanceへ波及しない |
| Vendor RX/TX EPSIZE、RX_NEED_ZLP、direct制限 | transfer単位・終端・受信完了条件 | E3。RX_NEED_ZLP=0は現TinyUSBでも既定であり新機能扱いしない |
| HID EP buffer、report/descriptor上限 | report定義から必要量、必要なら容量指定 | raw report/Report ID、GET/SET_REPORT、LED、boot protocol、HID複合 |
| MSC EP buffer/ブロック、storage helper | MSC config、storage interface、任意helper | 複数block、範囲外、read-only、media脱着、reset recovery |
| DFU XFER_BUFSIZE、runtime/DFU mode | DFU configとdescriptor同時生成 | 1024/4096、最終short block、拒否/検証失敗、detach、OTA helper独立 |
| NCM IN/OUT NTB個数・サイズ | NCM config、IP helperは別 | NTB境界、複数frame、pool満杯、切断、helperなしraw frame |
| Audio channel/sample/rate、control/packet buffer、方向・feedback | format/stream構成から算出、保持量だけ指定 | UAC1/UAC2、duplex、alt/rate切替、E5 |
| Video stream数、payload buffer、BULK | stream config、transport選択 | bulk/iso、payload境界、probe/commit、停止 |
| CCID buffer定数 | CCID configとprotocol上限 | 長さ境界、短い/不正APDU、busy/cancel |
| MIDI cable数 | instance configとdescriptor | cable0/最後/範囲外、双方向、複数Functionの能力判定 |
| config/report/string descriptor固定配列 | 二段生成で必要量、明示容量の任意指定 | 複合・文字列・BOS/WebUSB/MS OS長さ、短い読出し |
| EP0 size、alignment、DMA/FIFO選択 | controller能力と内部方針 | E4/E6。すべてを利用者設定へ露出させる必要はない |
| task/イベントqueue・統計 | ESP runtime option / opt-in観測 | E1/E2、未使用時コスト、overflow動作 |

基本Keyboard/Mouse、layout、NKRO、gamepad、consumer/system control、custom/vendor HIDも
同じraw report層へ載せ、新契約を対象にunitとpeer試験を新しく作る。旧試験への整合維持は求めない。
OEPのbuild_opt.hは最後の外部確認材料とし、USB関連overrideと内部`tud_*`呼出しを除去できるかを確認する。
OEPへ実際に変更を加えることは本ライブラリの準備作業には含めない。

## E8: 五台ベンチと未入手機種（初期試験の対象外）

| 順序 | 実験 | 合格の証拠 |
| --- | --- | --- |
| 1 | S3二台を固定しE0 | 両boardの開始/停止、代表通信、識別port |
| 2 | P4 FS Host→S2 Device、逆方向 | 列挙・CDC・reset/restart各20回、UARTログ独立 |
| 3 | P4 HS Host→S31 Device | HS negotiatedの記録、CDC/Vendor、両端停止 |
| 4 | 三台を固定しP4 HS/FSを交互に各20回 | 未使用controller停止、VBUS干渉なし、再upload後も同じ結線で再開 |
| 5 | S31 Host→P4 HS Device | Host対応成立後に実施。それまではPC HS等の別試験として記録 |
| 6 | 結線済みの組をarduino_combosへ追加し--combo allで実行 | 実配線の組だけ実行、必須集合をS2/P4 HSへ拡大、未設置は理由付き未検証 |

S2/S31の具体的board・connector・VBUS switchは未確定。購入候補の回路図とCore対応を確認し、
独立制御UART/JTAGと試験OTGを分ける。USB-Cの役割設定とVBUS sourceを役割交代ごとに表へ記録する。
両端がVBUSを駆動する配線や、HS信号をジャンパ線で常設する構成を採用しない。
この未確定はS3の段階2〜3を止めない。追加機材が必要な実験を「skipしたので対応済み」にしない。

## E9: 出荷判断

各classのOS依存箇所をWindows/Linux/macOSで確認する。特にCDC、HID、MSC、MIDI、
UAC1/UAC2、UVC、NCM、DFU、WebUSB/MS OS identityはEspUsbHostだけの成功で終えない。
長時間・反復の長さはE2/E5で出た故障時間を超えるよう決め、初回は1時間の通信と100回再構成から開始する。
新APIの出荷用exampleを作ってbuildし、移行表の未移行項目、残存TinyUSB参照、未確認SoC/OSを明記する。
v2比較値がないことはリリースを止める理由にしない。

## Solが判断を進めるルール

各作業は「仮説→最小実装→上記実験→採用理由」の単位にする。
既定値の選択やAPI名の整理で確認待ちにしない。失敗したら実装と観測を保存し、最小の再現へ縮めて再実行する。
同じ修正を根拠なく二回繰り返す場合は、IRQ/event/ownershipの時系列を追加してから再設計する。
難しい境界はSol Highへ上げるか、ユーザーが選んだ現モデルでレビューする。モデル変更自体を自動化しない。
別agentを常駐させることは前提にしない。
購入機材や対応対象の追加を選ぶ場面だけは事実と選択肢を残し、実機なしで成功を補わない。


## 2026-10-29 E1/E2の追加観測（cface78）

標準combo/noconftestのvendor_soakは1 passed、59.76秒、2 logs STOPPED/panicなし。
一意な32 bit連番を含む5/63/64/65/129/257/1024/4097 byteを1000フレーム送り、
各方向713000 byteを全payload比較した。Hostを25フレームごとに再起動・再列挙し、
39再接続、Deviceのreset hook80回を観測。CPU ringは非整列の外部257/509 byte、
転送staging128 byteでguardを維持し、終了後のheapは356328→356328。
実原本`/tmp/pytest-embedded/2026-10-29_04-36-15-691653/`、`.pytest-results/vendor-soak*`。

初回Hostは同期OUTの完了後にINを開始し、1024 byteで背圧と相互待ちになった。
Local EspUsbHostの実装/ヘッダでもvendorWriteは完了待ちであることを確認した。
Hostをcontinuous IN＋callback受信へ直すと成立した。Device側のbufferを広げる修正は行っていない。
初回の失敗もvendor-soak-initial-half-duplex-host*へ保持した。
この試験は完全なフレームの間で再接続し、転送途中の取消し/resetは既存の独立probeで確認する。

Core3.3.12/S3同一FQBNの小さいbuildを比較した。v2比較や測定frameworkではなく、未使用参照の切分けである。

| probe | Flash (CLI) | globals (CLI) |
| --- | --- | --- |
| no_library | 259465 | 21584 |
| include_only | 260633 | 21648 |
| device_only（beginなし） | 263081 | 21880 |

includeだけの差はFlash1168/global64 byte。nm/mapで、未使用EspUsbDeviceNetif.cpp.oの
IPAddress constructor参照がArduino core.a(IPAddress.cpp.o)を取り込み、INADDR_NONE/IN6ADDR_ANYと
その初期化・printTo、netif_listを残すことを確認した。Native USB task/PHYの自動開始を検出した結果ではない。
ELF/map/nm/build.optionsとtranscriptは`.pytest-results/unused-before-archive*`へ保存した。

候補はlibrary.propertiesのdot_a_linkage=trueで、個々の.oの直接リンクをarchiveへ変える。
[Arduinoの公式仕様](https://docs.arduino.cc/arduino-cli/library-specification/)にある標準propertyで、
利用側のbuild_optや内部型を増やさない。次にinclude差分と全classのリンク/実通信を確認して採否を決める。


### archiveリンクの小規模実測

library.propertiesへ`dot_a_linkage=true`を加え、同じinclude_only/device_onlyを再buildした。
Arduino標準のarchive生成をmap/fileで確認し、include_onlyはライブラリなしと同じ
Flash259465/global21584 byteとなった。差分は両方0 byte。
Device宣言のみはFlash261913/global21824 byteで、IPAddress globals/netif_listと
未選択Function state、USB startupのsymbolはない。宣言したDeviceと終了処理の共通費用は残る。
この値はCore3.3.12/S3の最小probeの結果であり、他のCoreや任意のアプリへの固定値ではない。

ELF/map/nm/build.optionsは`.pytest-results/unused-after-archive/`、
集計は`unused-after-archive-audit.json`へ保存した。設定は利用者のbuild_optへ移さずlibrary側へ置く。
現行8 examplesの3 profilesは全24ビルド成功。各ELFのFQBN/機械種別、archive、
29 source依存、native controller symbolとaddr2line、未選択Net/Video/CCIDの除外を確認した。
集計`.pytest-results/archive-examples-audit.json`、各transcriptも同prefixへ保存。
製品は052bcbbで固定し、実行中の追加作業は別probeと文書のみ。
全classのリンク・override hook・実通信を含む全44件は実行中。
