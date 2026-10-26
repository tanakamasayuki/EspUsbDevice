# EspUsbDevice v3 開発計画

作成日: 2026-10-25。調査基準: EspUsbDevice 2.6.1、commit `b1094cd`。

2026-10-26更新: 開発用試験は既存test/sampleから独立した`validation/`へ変更した。
初期実機はS3–S3だけ。conftestによる独自制御は行わない。
Vendorのinstance資源と借用TXを実装し、S3–S3で検証した。現在の契約は[Vendor API](V3_VENDOR_API.ja.md)。
[引き渡し](V3_HANDOFF.ja.md)、[API変更の判断](V3_API_DECISIONS.ja.md)、
[所有権の出発案](V3_ARCHITECTURE.ja.md)、[実験台帳](V3_EXPERIMENTS.ja.md)を参照。

v3は、現在の明示的な構成と基本的な使い勝手を軸に、使う機能だけにコード・メモリ・実行コストを
持たせるmajor releaseとする。公開APIを整理し、ビルド時の容量・転送方式設定を実行時の設定へ移す。
最終設計の一貫性を互換性より優先する。基本例はなるべく同じ操作感にするが、よりよい設計になるなら変更できる。
最終形ではTinyUSBのクラス処理、共通スタック、DWC2ドライバへの依存を撤去する。
段階移行中は既存の下位層を利用できるが、二重バックエンドを恒久的な公開仕様にしない。

## 維持する基本方針

- Deviceに必要な機能を明示的に付け、設定して`begin()`する。USBの自動起動を要求しない。
- port、speed、descriptor、endpoint、raw report / transferを明示的に扱える。
- HID usageとraw reportを第一級APIとし、文字列などの便利な操作も提供する。
- Arduino-ESP32標準USB APIとの完全互換は目標にせず、標準USB Device stackとは排他利用する。
- Arduino、ESP-IDFのSoC・PHY・FreeRTOS・heap/cacheは利用する。networkやstorage helperは任意とする。
- USB Audioの責務はclassとPCM境界までとし、I2S・codec・DSPなどを必須依存にしない。
- 実機試験でAPIと実装を固める。特定のアプリケーションやHost実装への専用設計にしない。

これは[従来の基本方針](DEVELOPMENT_PLAN.ja.md)の継続である。変更するのは内部スタック、
設定の時期、詳細APIと互換性の範囲であり、ライブラリの用途と責務ではない。
CH32RVは設計事例とAPI調整の相手であり、移植元ではない。OEPは高度な利用例の一つとして扱う。

## コストの設計と必要な確認

未使用クラスを共通のdriver表やcallbackから参照してリンクさせない。大きな静的バッファを全クラス分
持たず、インスタンス数と設定に応じて確保する。自動確保と利用者提供バッファの両方を設計対象にする。
コンストラクタでは登録と軽量な状態保持を基本とし、重い資源は開始時または任意機能の有効化時に確保する。
通常の転送経路では繰り返しheap確保しない。待機用同期オブジェクト、観測、統計、network helperも
未使用時のコストを確認する。コントローラのendpoint数・FIFO容量などの物理上限は残る。

現行版との網羅的な比較ベンチマークは開始条件にもリリース条件にもしない。
既存版のcommitと依存versionを記録し、比較が必要になった時点で同じCore・sketch条件で測る。
開発中はv3自身が設計どおり必要な資源だけを使い、正常に転送・回収できることを確認する。

| 構成 | 測定と合格条件 |
| --- | --- |
| ライブラリなしとincludeのみ | Flash、data/bss、リンクsymbolを比較。includeだけでUSBのtask・PHY・クラスbufferを生成しない |
| オブジェクト宣言のみ | オブジェクトと登録の費用を分離。`begin()`前の重い資源確保がない |
| Keyboard単体、CDC単体 | 未使用クラスの実装・bufferが残らない。共通部分の費用を説明できる |
| 複合構成、複数CDC | 必要な個数に応じた増分になり、最大数分の一括確保をしない |
| 通常送信と直接送信、任意helper | 転送と任意機能が成立する。CPU負荷・転送量・遅延は設計判断や問題切り分けに必要な範囲だけ測る |
| 終了・再開始・確保失敗 | task、heap、DMA buffer、endpoint、callbackを回収し、反復で増加しない |

link map、簡単なsize確認、開始・終了時の資源確認を中心にし、測定専用基盤は作り込まない。
削減率やv2より速いことを一律の合格条件にしない。リーク、転送欠落、周期性、停止不能などの
正しさに関わる試験は維持する。高負荷や連続運転は安定性確認として実施する。

## 公開APIと互換性

基本例のDeviceと機能オブジェクトの宣言、`begin(config)`、識別文字列、Keyboard/Mouseの基本操作、
CDCの`available/read/write/print`は自然な形であれば維持する。
旧sampleと旧testを今回の設計・合否基準にしない。新しい利用コードから使いやすさを確認する。
基本例・複合例の最終形を先に書いて、意味が明確で設定が簡潔かを確認する。

基本APIも詳細APIも、統一感・資源所有・最適化のために変更できる。互換性のためだけのshim、
旧名alias、二重状態を作らない。作業中は既存exampleや利用側がbuild不能でもよく、旧APIを動かすために
中間の互換層を作らない。段階の対象になった機能を新APIで動かし、リリースまでに出荷exampleを揃える。
変更ごとに旧API、新API、利点、不利益、既存利用への影響と移行例を記録する。
破壊的変更が可能でも、変更に利点のない名前や通常操作まで一律に変更しない。

| 整理対象 | v3で定める契約 |
| --- | --- |
| 設定とlifecycle | 開始前設定、`begin/end`、detachと停止の違い、開始中の変更可否、失敗時のrollback |
| バッファ | 容量と転送単位とpacketサイズの区別、所有者、alignment、DMA可否、解放可能時点 |
| 送受信 | コピーと直接送信、受信FIFOとcallback、待ちとtimeout、short packet/ZLPと転送完了条件 |
| 完了通知 | 成功・中断・切断・resetでのbuffer返却、callback context、再入、完了通知からの次転送 |
| 通知とpolling | アプリ向け遅延通知と高速転送完了の使い分け。同名APIの意味をクラス間で統一 |
| 状態と能力 | started、configured、suspended、設定値、実効値、ハードウェア上限を分離 |
| descriptor | interface識別情報、HID usage、割当interface/endpointの照会、独自class拡張の入口 |
| エラー | 誤設定、資源不足、一時的なbusy、転送失敗を識別し、無言の切り詰めや無視を避ける |

`writeCapacity/transferSize/maxReportSize`などのstatic APIはインスタンスの実効値へ整理できる。
`CFG_TUD_*`、TinyUSB内部型・関数、`startTinyUsb`は利用者APIから外す。
CH32RVとは名前だけでなく、戻り値、通知の粒度、buffer寿命、callback contextを比較する。
ESP固有のtask/core/cache設定は共通概念と分ける。CH32RV側の変更は仕様案を共有する対象とし、本計画の実装範囲には含めない。

## 開発環境と段階

実装は`v3`ブランチと別worktreeで進める。v2のcommitを記録し、比較用checkoutは必要時だけ作る。
Core、Host版、plugin版、board revision、build option、結線を試験結果とともに記録する。
小さな実装単位と実機確認を繰り返し、全APIの事前確定や別製品の試作スタックは作らない。
変更中に未対応となるクラスは明記し、各段階の完了時にはその段階の対象構成を新APIでbuild・実行できる状態にする。
旧クラス・旧sample・旧testの整合維持は開発途中の要件にしない。新しい試験は新APIの契約から作る。
未実装と失敗を隠すために新試験を削除・無条件skipしない。

| 段階 | 作業と成果物 | 次へ進む条件 |
| --- | --- | --- |
| 0 試験環境 | 独立したvalidationにS3–S3のみ。標準pluginと普通のtest関数で新規probeを実行 | conftest/helper依存なしでbuild/upload/通信/停止を確認 |
| 1 API判断 | TinyUSB制約による不自然な操作を抽出し、before/after、利点、不利益、移行負担を比較 | 最初はVendorの短い返信・容量・転送方式。実験と採用基準を定める |
| 2 Vendorから実装 | copy writeの自然な送出、instance別容量、再構成、borrowed転送。必要なregistry/lifecycleだけを同時に実装 | S3でE1/E2/E3、build_optなしの設定切替、返却、未使用費用を確認 |
| 3 共通性の確認 | CDC、HID、最小Audioへ展開。別instanceと複合構成で契約を確認 | byte stream/report/周期転送で責務が自然。P4 DMA/cacheは後段でE4を実施 |
| 4 全クラスへ展開 | HID各種、複数CDC、MIDI、MSCとhelper、Vendor、NCMと任意network、CCID、DFU/OTA、UAC1/UAC2、Video | 現行機能の移行表と代表複合構成が埋まり、設定がAPIまたは自動算出に移る |
| 5 共通スタックとDWC2 | EP0・標準要求・イベント・転送管理・DWC2を独自実装へ切り替え。クラス変更とはcommitと比較を分ける | reset、SETUP割込み、STALL解除、alternate setting、suspend/resume、remote wakeup、DMA/FIFOを確認 |
| 6 対応機種とリリース | S2実機、P4のFS/HS、OS互換、長時間・再構成試験、新APIの代表example、移行ガイド、依存監査 | 新APIの基本例、機能回帰、資源確認、TinyUSB依存撤去が揃う。S31は新規対応として別に判定 |

段階2〜5の内部境界は実装結果で調整する。旧・新クラスが同一interfaceをclaimする構造や、
不足symbolがArduino同梱TinyUSBへ解決されるfallbackは認めない。
DWC2由来コードを参照・改変した場合はライセンスと出所を保持し、依存撤去と出所表示の削除を混同しない。

最初の見積もり対象は段階0〜3とし、APIと共通転送の成立後に残りを再見積もりする。
全機能・DWC2までの以前の概算11〜20人週は予算検討用の幅としてのみ扱う。
新API整理、ベンチ整備、S31のCore/Host対応、機材調達待ちは別途見積もる。日付で次段階へ進めず、上記条件で判断する。

## 開発モデルと作業の進め方

主担当はSol中心とする。利用可能ならGPT-6.1 Solを候補にし、通常作業はMediumまたはクライアントの
既定の推論設定で開始する。公式OpenAI資料でも複雑なcodingにはGPT-6.1 Solを案内している。
以下の作業分担は本プロジェクト向けの提案であり、USB開発での性能保証ではない。
[モデルの案内](https://learn.chatgpt.com/docs/models)、[選択の指針](https://learn.chatgpt.com/docs/model-selection)

| 作業 | モデル運用案 | 確認点 |
| --- | --- | --- |
| 段階0の独立S3–S3試験 | Sol Medium | 標準pluginだけで新規probeを実行。拡張不足はplugin側へ提案 |
| 段階1の所有権・通知・APIの骨格 | 現在のモデルでまとめてからSolへ渡す、またはSolの高い推論設定で作成 | 基本・複合・streamingの利用例と状態遷移で矛盾を確認 |
| 段階2〜4のクラス実装・example・試験 | Sol Mediumを基本とし、複雑な変更だけHigh相当へ上げる | 一つの機能経路を実装し、対象試験を通してから広げる |
| EP0、DWC2、DMA/cache、割込みとの競合 | Sol High以上を候補とし、重要な境界で現在のモデルまたはAstraによる別のレビューを検討 | 状態遷移、同期、buffer寿命をコードとログで確認 |
| 再現条件が不明な停止、設計の矛盾 | 再現・観測を揃え、Solで進展しなければ現在のモデルまたはAstraへ切替 | 同じ修正案の反復や実機根拠のない仕様変更を止める |
| 文書・既に決めたAPIへの置換・リリース確認 | Sol Medium | API対応表、全example、未検証項目の整合 |

モデルは段階ごとの自動切替を前提にしない。ユーザーが選択したモデルで作業を続けられる計画にする。
Solだけで進めてもよく、重要箇所のレビューは別セッションで同じSolに行わせる方法もある。
レビューは常駐の別agentを必要とせず、原則一つの実装作業を逐次進める。
モデル変更のために同じ実装を作り直したり、広い全体レビューを毎回行ったりしない。
モデルの切替はユーザーがクライアントの選択欄、CLIなら`/model`で行う。モデル設定の変更は本計画の文書更新には含めない。

### 最初の作業単位

1. Solで新しいS3 Vendor probeを使い、短いwriteの自動送出と明示batchを比較する。
2. API変更の利点・不利益・既存利用への影響を記録し、採用する操作を決める。
3. Vendorのinstance configと所有権を実装するために必要なregistry/lifecycleを作る。
4. copy/borrowedと再構成が通ったらCDCへ展開し、HID/Audioとの複合で共通設計を見直す。

各単位は「目的、今回の範囲、完了条件、必要な試験」を示して依頼する。単なる調査だけで終えず、
実装・対象試験・記録までを一単位とする。設計の改善に必要な関連箇所の変更は許容する。
新しいモデル/セッションに渡す情報はリポジトリに残す。

- 現在の段階、branchとcommit、既知の未対応classと一時的なbuild失敗。
- 採用したAPIと所有権の判断、変更理由、次に実装する一単位。
- 試験コマンド、結線profile、今回の結果とログ、実機未確認の項目。

例として「v3計画に従いCDCのbuffer所有と開始/終了を実装し、S3 peerで双方向通信と再開始を確認する。
旧API互換層と性能ベンチは追加せず、次の単位が着手できる状態を記録する」のように依頼する。
費用は一回の推論量だけでなく再調査・手戻りを含めて判断し、進展がある間はモデルを頻繁に変えない。

## 用途別の検証

| 用途 | 主な観点 |
| --- | --- |
| 最小HID | Keyboard/Mouse、raw report、LED、protocol、layout、最小コスト |
| 通常通信 | CDC、MIDI、Vendor、短い要求応答、複数port/cable、backpressure、timeout |
| 複合デバイス | HID+CDC、CDC+MSCなど、資源配分、上限拒否、各機能の独立性 |
| 大容量通信 | MSC、Vendor、NCM、コピー/直接転送、短い末尾、packet倍数長、ホスト停止 |
| 周期通信 | Audio再生/録音/duplex、feedback、UAC1/UAC2、Videoのalternate settingと連続転送 |
| 更新と拡張 | DFUのblock size、OTA成否、WebUSB/MS OS descriptor、CCID、独自control request |
| lifecycleと異常系 | 開始失敗、再構成、転送中のreset/切断、cancel、STALL復旧、欠落/重複通知 |

OEPは大容量通信と複合構成の外部利用例として追加する。OEPのprotocolやqueue優先順位はライブラリへ取り込まない。
Hostとの相互誤解で試験が成功しないよう、PC OSや独立したhostによる列挙・通信試験も残す。
Host側の不具合は再現条件を分離し、Device側の非準拠なworkaroundで隠さない。

## 独立した試験環境

開発用入口は`validation/`。旧`tests/`と`examples/`を読み込まず、新規sketchとtestを作る。
別のpyproject/lock/venvを使い、標準pytestの`--noconftest`でconftest読込みを無効にする。
独自hook、fixture差替え、nodeid集計、既知ログ許可表、互換helperを置かない。
通常のtest本文から開始・通信・停止を明示し、pluginのdut/peersとprofile/port/lockを使う。

```sh
cd validation
uv sync --locked
uv run --locked --env-file .env pytest --combo all --collect-only -q
uv run --locked --env-file .env pytest --combo all -q -rs --junitxml=.pytest-results/v3-all.xml
```

初期profileはS3 HostとS3 Deviceに一つずつ。P4やlocal複製を増やしてskipする仕組みは作らない。
plugin 1.10.0の`arduino_combos`に結線済みの組を列挙し、`--combo all`を標準入口とする。
現時点は`s3: primary=s3_host,device=s3_device`だけ。単独選択は`--combo s3`。
将来は対応sketchのprofileと成立する結線のcomboを増やす。USB結線を直積や独自skip hookで表現しない。
必要な機能が不足するなら、再現例・必要な結果・受入条件をまとめてplugin側への追加依頼を優先する。
conftestは標準化できない必要性が実証された場合の最後の手段とし、別ファイルへ移した独自hookで代替しない。

plugin 1.10.0のrelease guideのprofile組合せ、`--combo`、peerの章を再確認した。
`--combo all`は対応profileのない組を展開せず、適合する組がないtestは`combo=none`でskipする。
port未設定によるskipは未検証、設定済みportへの接続やbuild/upload失敗はerror、通信不一致はfailed。
全件skipを失敗にする標準optionは追加候補として記録し、現在は今回の結果が9 passedであることを確認する。
終了値0だけを合格としない。旧`--usb-require`の独自監査は撤去した。
試験は逐次実行し、compile省略時もuploadは行うというpluginの通常動作に従う。

## 機種と常駐ベンチ

開発初期は既存S3二台peerを基準とする。P4はDMA/cacheとFS/HS差分の確認時に追加する。
P4一台loopbackは両コントローラを試せるが実linkはFSであり、HS合格の代用にはしない。
S2は現行対応の実機確認、S31はCore/PHY/能力表を含む新規対応として扱う。

### 三台を共有する結線候補

提示された次の結線は、常駐機材を共有する有力候補とする。線はUSB data linkを表し、
ブリッジ動作や両側同時実行を意味しない。

```text
S31 OTG ── HS ── P4 HS OTG
                 P4 FS OTG ── FS ── S2 OTG
```

| 試験 | Host | Device | 条件 |
| --- | --- | --- | --- |
| S31のHS Device | P4 HS | S31 | S31 Device対応後。P4 FS側は停止 |
| S2のFS Device | P4 FS | S2 | P4 HS側は停止 |
| P4のFS Device | S2 | P4 FS | S2 Hostの実機成立を先に確認 |
| P4のHS Device | S31 | P4 HS | S31 Host対応が別途必要。現行EspUsbHostの対応を仮定しない |

現行EspUsbHostはP4で選択した一つのHost peripheralをinstallする構成である。
ESP-IDF 5.5.1の資料にも同時にHostとして動くperipheralは一つという制限があるため、
まず停止・deinitまたは再起動を挟む逐次試験を前提とする。
新しいCoreでdual Hostが利用できても、ベンチ共有のためにその対応を必須にしない。
[ESP-IDF USB Hostの制約](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32p4/api-reference/peripherals/usb_host.html#features-limitations)

S31にはHS OTGと別のUSB Serial/JTAGがあるが、現行EspUsbHostはS31対応を掲げていない。
HSリンクを張れるハードウェアと、試験用Hostソフトウェアの完成を区別する。
S31 Hostが未対応の間、P4 HS DeviceはPCのHS Hostまたは一時的な別P4で検証する。
[S31のUSB構成](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s31/schematic-checklist.html#usb)

### 常駐は五台を基本とする

S3二台peerと上記S31/P4/S2の三台、合計五台を常駐ベンチの基本案とする。
既存のS3標準環境を維持し、他機種はP4を共有する。S2/S31の未入手期間もS3で開発を進める。
四台への削減、USB切替器の追加、S3の標準Host変更はv3の作業目標に含めない。
将来必要になれば、役割交代とHost対応の実績をもとに別途検討する。
三台共有部分と役割交代は、採用前のbench bring-upを必要とする。

常駐ベンチの最終決定は、USB配線を固定したまま停止・書き込み・再起動・役割変更を反復できることを条件にする。
Host二重起動を防ぎ、未使用側を停止してから次のprofileへ移る。共有P4は同じ物理資源としてlockする。
HSは適切なUSBケーブル・コネクタで配線し、ジャンパ配線を前提にしない。
具体的なpin・connector・VBUSは購入するboardの回路図で確定する。独立給電時の逆給電、
Host役割交代時のVBUS source競合を防ぐ構成にする。USB-Cのrole設定や電源switchも確認する。

### ベンチを決める最初の実験

1. 新規S3–S3 probeで治具と試験環境を確認する。性能比較は不要。
2. P4 FS HostとS2 Device、逆のS2 HostとP4 FS Deviceを個別に確認する。
3. S31入手後、P4 HS HostとS31 Deviceを確認し、実際のnegotiated speedを記録する。
4. 三台共有結線でP4のHS/FS試験を逐次反復し、停止・port選択・VBUSの干渉がないことを確認する。
5. S31 Hostが利用可能になった段階でP4 HS Deviceを確認する。未対応ならPC等で代替し、同等と偽らず試験種別を記録する。
6. S3二台と三台共有部分を結線表へ登録し、一括試験で対応する組だけが逐次実行されることを確認する。

## v3の完了条件

- 新APIの基本例が簡潔で一貫しており、旧APIから変更した箇所には移行表がある。
- 全現行クラスの単体・代表複合構成が検証され、任意helperは必須依存になっていない。
- 未使用機能が重い資源を持ち込まず、開始/終了と失敗時の資源管理が確認できる。v2との性能比較は必須にしない。
- 容量・個数・転送方式はAPIまたは構成から決まり、利用側にTinyUSB設定や内部関数を要求しない。
- TinyUSBの上位/下位依存と意図しないCore同梱版へのリンクがなく、必要な出所・licenseは保持する。
- 機材拡大時の一括試験は標準plugin機能を基本にし、未実行を合格扱いしない。必要な共通機能はplugin側へ追加する。
- S3、P4 FS/HS、S2の確認状況とS31新規対応状況を区別し、未検証を対応済みにしない。
- Windows/Linux/macOSの対象class、長時間試験、Firmware更新などの結果と残課題をリリース判断に残す。

## 次に着手する作業

[API変更の判断](V3_API_DECISIONS.ja.md)に沿ってVendorの短い送信と容量・modeを優先する。
[引き渡し](V3_HANDOFF.ja.md)の独立S3–S3 probeから着手し、APIごとに影響と実験結果を記録する。
旧test/sampleへの追従や、汎用registryの先行完成を目的にしない。
