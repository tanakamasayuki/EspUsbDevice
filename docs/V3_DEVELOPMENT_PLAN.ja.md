# EspUsbDevice v3 開発計画

作成日: 2026-10-25。調査基準: EspUsbDevice 2.6.1、commit `b1094cd`。

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
既存の基本sketchは使いやすさの確認材料にし、変更不要であることを絶対条件にはしない。
基本例・複合例の最終形を先に書いて、意味が明確で設定が簡潔かを確認する。

基本APIも詳細APIも、統一感・資源所有・最適化のために変更できる。互換性のためだけのshim、
旧名alias、二重状態を作らない。作業中は既存exampleや利用側がbuild不能でもよく、旧APIを動かすために
中間の互換層を作らない。段階の対象になった機能を新APIで動かし、リリースまでに出荷exampleを揃える。
変更ごとに旧API、新API、理由、利用側の修正例を記録する。

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
全クラスや旧APIが全commitで動くことは要求しない。未実装と失敗を隠すためにテストを削除・無条件skipしない。

| 段階 | 作業と成果物 | 次へ進む条件 |
| --- | --- | --- |
| 0 試験環境 | 現行commitと依存版を記録。plugin更新、profileと結線表、一括実行、必須試験の判定を整備。既存S3の代表通信で治具を確認 | 対象外skipと実行失敗が区別できる。比較ベンチマークは不要 |
| 1 API設計 | ESP/CH32RV/v3の対応表、基本例、複合例、streaming例の利用コード、所有権・通知契約を作成 | 最終APIと変更理由が明確。未使用機能がリンクされない構造を説明できる |
| 2 小さな実装 | HID Keyboard、CDC、両者の複合を新しい資源管理で動かす。必要なら旧EP0/DWC2を内部境界で利用 | S3 peerの機能、未使用クラス除外、確保失敗、終了・再開始を確認 |
| 3 異なる転送方式 | Vendorの通常/直接転送と受信、Audioの最小周期転送を実装。P4でDMA/cacheの成立性を早期確認 | 終端・中断・buffer返却・周期性を確認。共通設計を見直してから全classへ展開 |
| 4 全クラスへ展開 | HID各種、複数CDC、MIDI、MSCとhelper、Vendor、NCMと任意network、CCID、DFU/OTA、UAC1/UAC2、Video | 現行機能の移行表と代表複合構成が埋まり、設定がAPIまたは自動算出に移る |
| 5 共通スタックとDWC2 | EP0・標準要求・イベント・転送管理・DWC2を独自実装へ切り替え。クラス変更とはcommitと比較を分ける | reset、SETUP割込み、STALL解除、alternate setting、suspend/resume、remote wakeup、DMA/FIFOを確認 |
| 6 対応機種とリリース | S2実機、P4のFS/HS、OS互換、長時間・再構成試験、全example、移行ガイド、依存監査 | 新APIの基本例、機能回帰、資源確認、TinyUSB依存撤去が揃う。S31は新規対応として別に判定 |

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
| 段階0のplugin・profile・結線条件整備 | Sol Medium | 収集結果、必要な組の実行、誤ったskipがないこと |
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

1. 現在のモデルまたはSolで、資源所有・状態遷移・通知context・基本/複合/streaming例の骨格を短い設計文書にする。
   未使用コストと最終形に直結する判断を先に決め、全クラスの細部は実装で調整する。
2. Solで段階0の試験環境を整え、S3の既存代表通信を実行する。v2の性能測定は行わない。
3. Solで段階2を「登録と開始/終了」「Keyboard」「CDC」「複合」の検証可能な単位に分けて進める。
4. 最初の複合構成が通ったところで、APIと所有権のレビューを一度行う。問題がなければ同じ進め方で段階3以降へ進む。

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

## 最新pluginによる一括試験

参照したpluginは`~/dev/pytest-embedded-arduino-cli`の1.9.0、commit `5ef1658`。
`TESTING_ADVANCED.ja.md`の「profile の組合せと適用条件」と`README.ja.md`の実行modeを基準とする。
`pytest_arduino_cli_skip_reason`のhook登録はpluginの実装でも確認した。
段階0で`tests/pyproject.toml`とlockを検証済み版へ更新する。現行の`>=1.6.0`だけでは必要機能を保証しない。

### 候補の展開と結線による除外

標準の入口は、各sketchのprimary/peer profileを展開する一括実行とする。
以下は段階0でprofile・結線条件・依存更新を整備した後のコマンドである。

```sh
cd tests
uv run --env-file .env pytest --profiles=all --peer-profiles=all --collect-only -q
uv run --env-file .env pytest --profiles=all --peer-profiles=all -rs --junitxml=report-v3.xml
```

`--profiles=all`は全チップの自動発見ではなく、各`sketch.yaml`に書かれた候補の展開である。
`--peer-profiles=all`との直積から、実際の結線・役割・コントローラに適合する組だけを実行する。
`--run-mode=all`はbuild/upload/testの実行modeであり、profile展開とは別である。

- 結線表を一か所に置き、`tests/peer/conftest.py`の`pytest_arduino_cli_skip_reason(item, combination)`で共通条件を適用する。
- 個別のclass/速度条件は`arduino_skip_if`を使用できる。理由を必須にする。
- `combination.primary`と`combination.peers["device"]`を役割表へ解決して判定する。profile名の一致で結線を判断しない。
- 現在の`(s3_peer_host, s3_peer_device)`は名前が異なるが正規の組である。
- local Host profileと重複Device profileも`all`に入る。通常はreleased Hostの組を必須とし、local組は設定で明示的に有効化する。除外理由を残す。
- 判定は設定だけを読み、収集中にport接続やfixture取得をしない。`peers`はfixture依存に明示する。
- 配線による制限をunitやsingleへ伝播させない。`.ino`を持たないunitは一度だけ実行する。
- build-onlyでは結線不足を理由にcompileを省かない。配線条件を実機modeに限定し、機種の機能制約とは分ける。全exampleのbuild matrixも維持する。

### portと結果の取り扱い

複数候補の役割はprofile別のport設定を使い、共通`--port`や共通環境変数を流用しない。
例としてprimaryに`TEST_SERIAL_PORT_S3_PEER_HOST`、peerに
`TEST_SERIAL_PORT_PEER_DEVICE_S3_PEER_DEVICE`を設定する。実機の識別と結線は別々に記録する。
S2の書き込み・ログ用UARTも含め、試験USBとは独立した制御経路を用意する。

| 状況 | 結果 |
| --- | --- |
| 結線のない組、対象外の機能、通常実行対象外のlocal組 | build前の理由付きskip |
| 組合せmodeでport未解決、同じ物理portを二役に割当 | pluginの設備skip。未検証として記録 |
| 設定済みportの接続失敗、compile/upload失敗、lock timeout | error |
| 実行した通信・状態検証が不一致 | failed |
| teardown失敗 | error。次の構成を安全に実行できると仮定しない |

`--collect-only`は適用条件を確認できるが、port不足・接続状態の確認はしない。
全件skipでもpytestは通常終了値0になるため、今回のJUnit結果から必須構成と必須caseが
実行・成功したことを別途検証する。過去の`state.json`やbuild-onlyの結果を実機成功に数えない。
必須集合は開発段階ごとに定義し、初期はS3 peer、後段でP4 FS/HSとS2を加える。
S31未入手・未対応は明示し、v3既存機種の合格をS31対応済みと解釈しない。

### 反復試験の時間

実機の組合せ実行は逐次とし、xdistで並列化しない。同じP4を使うHS/FS試験も直列化する。
既存の開始待機・停止応答・serial error監査を維持し、nodeidへの組合せ追加後も監査条件が一致することを確認する。
通常は対象caseだけを増分buildで反復し、Core/依存変更、profile切替、段階完了時はclean検証する。
Host側の再compile削減は実際のcacheと依存追跡を確認してから採用する。
`--run-mode=test`はcompileを省くがuploadは行い、Deviceだけを再buildするoptionではない。
Hostの書き込み省略を前提に計画しない。

単独切り分けも維持する。全体ではprofile名が異なるため、単一`--profile`指定は対象suiteを限定する。

```sh
uv run --env-file .env pytest peer/ --profile=s3_peer_host -rs
uv run --env-file .env pytest loopback/ --profile=p4_loopback -rs
```

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

1. 既存S3 peerの代表通信で治具と試験環境を確認する。性能比較は不要。
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
- 一括試験は結線対象だけを実行し、必須caseの未実行を見逃さない。個別実行でも同じ試験ができる。
- S3、P4 FS/HS、S2の確認状況とS31新規対応状況を区別し、未検証を対応済みにしない。
- Windows/Linux/macOSの対象class、長時間試験、Firmware更新などの結果と残課題をリリース判断に残す。

## 最初に着手する作業

APIと所有権の骨格を短く定め、段階0でplugin 1.9系とprofile別port・結線条件・必須結果監査を整える。
現行commitと依存版を記録し、既存S3の代表通信で環境を確認する。比較ベンチマークは後回しにする。
v3 worktreeでSol中心に最小構成を実装し、複合構成の成立時点で設計レビューを行う。
この計画の作成時点では、実装変更、branch/worktree作成、plugin依存更新、実機への書き込みは行っていない。
