# v3 API変更の優先順位と判断理由

2026-10-26。最初の対象はS3–S3上のVendor通信。旧sample・旧testへの追従ではなく、
「設定する→開始する→送受信する→終了して必要なら設定し直す」という利用の流れを基準にする。
互換性を壊してよいことと、変更する価値があることは別である。既存利用への影響は公開headerと
実装から確認し、APIごとに利点・不利益・移行方法を記録してから変更する。

## 最初の利用経路

短い要求応答、連続stream、複数の独立interfaceを対象とする。OEP一件の最適化にはしない。
最初にcopy経路を自然にし、次に同じ実行バイナリで容量・転送方式を変更できるようにする。
registryや抽象層はこの経路に必要な分だけ実装する。Keyboardを動かすための全体骨格を先に完成させない。

現行の制約は`src/EspUsbDevice.h`、`src/EspUsbDevice.cpp`、
`src/internal/EspUsbTinyUsbConfig.h`、`src/class/vendor/vendor_device.*`を根拠にする。
旧testの期待値は新仕様の正しさを決める根拠にしない。

| 優先 | 現状と変更案 | 利点 | 不利益・既存利用への影響 | 採用前の確認 |
| --- | --- | --- | --- | --- |
| 1 | Vendorの短い`write()`も自動で送信を進める。CDCは現在もwrite後にflushしており、Vendorだけ手動flushが必要 | 短い返信がbuffer内で止まらない。Print系の通常利用が揃う | writeを複数回まとめていたstreamでは転送予約回数が増え得る。USB read境界は元々保証されないが、short packetの出方に依存した利用は要確認 | 新probeで1/63/64/65/127/128/129 byte、続けて複数回write。まとめ書きが必要なら明示batch設定を比較する |
| 2 | TX/RX容量とcopy/borrowed方式をinstance configにする。begin前に指定しend後に変更可 | build_opt.hとclean rebuildが不要。別instanceに設定が波及しない | begin時の確保失敗が生じ得る。静的bufferを前提とする利用には借用buffer経路が必要。v2クラスdriverのままでは容量APIだけ追加しても実体が動的にならない | 同じbinaryで小容量→大容量→小容量。未使用modeのbuffer非確保、rollback、再開始を検証 |
| 3 | direct送信をbuild flagで有効化する方式を廃止。copy writeとstrictな借用送信を選べる | 配布binaryで選択可。コピーしない契約が明確 | 借用中bufferを書き換えられない。direct対応を名乗るだけで内部copyする案より利用条件は厳しい | 拒否時は借用しない。成功/取消し/reset/切断で一度だけ返す。S3で証明しP4 cacheは後段 |
| 4 | RXをstream readとborrowed bufferへ整理。callback登録の有無だけで暗黙に転送方式を変えない | 同じAPIがビルド設定次第で無効になる状態をなくす | modeの指定が一つ増える。callback pointerの保持期限に依存したコードは修正が必要 | 未投稿/満杯のbackpressure、短い末尾、再投稿、停止時返却 |
| 5 | `writeCapacity()`等をinstanceの実効値へ。要求値・確保容量・MPS・transfer上限を分離 | 設定した量と使用可能量が分かる。TinyUSBのstatic上限を利用者に意識させない | `EspUsbDeviceVendor::writeCapacity()`のようなclass指定呼出しは変更が必要。object経由の呼出しはなるべくそのまま | 2 instanceの値が独立。begin前/失敗後/end後に何を返すかを明記 |
| 6 | 稼働中設定と不可能な容量を明示拒否。`waitWritable()`の要求容量を黙ってclampしない | 要求と実行結果の不一致を発見できる | 過大な容量を指定しても動いていた利用はエラーになる。呼出側に修正が必要 | 容量+1、overflow、不適合bufferで拒否し現状態を保持する |

## 変更を小さくする意味

`Device`とFunctionの関連付け、`begin(config)`、`available/read/write/print`という操作感は活かす。
名前を統一するだけの全面renameや、通常送信に必ずticket管理を要求する変更は最初に行わない。
共有するのは所有権・戻り値・設定時期の意味であり、HID reportとAudio frameをbyte streamへ潰すことではない。
一方、容量やmodeを内部TinyUSB定数へ丸める互換wrapperは作らない。
実装の都合で旧testへ合わせるために、無効なAPI・二重状態・旧backend選択を残さない。

copy writeは受け入れたbyte数を返し、wire転送完了とは区別する。
通常writeを自動送出にしても、一回のwriteをメッセージ境界やZLP挿入点と同義にはしない。
`flush()`の現在の意味（送出の促進）を勝手に無期限の完了待ちへ変えない。
完了待ちを追加するならtimeoutと停止時の結果を持つ別操作として比較する。

direct転送の安全な返却に必要なticket/statusは内部から導入する。
公開APIは「単発の借用送信」と「queueを使うstream」の両方の利用コードで比較する。
既存の`writeDirect()`という名前を残すか、結果型付きsubmitへ変えるかは、
切断/cancelを含む呼出側コードと移行量を見て決める。新しい抽象語へ置き換えること自体を目的にしない。

## API変更ごとに残す記録

1. 利用者が困る具体的な操作と、現在その制約が発生するコード。
2. before/afterの短い利用コード。基本利用、stream、複合のうち影響するもの。
3. 削減される設定・手順・資源と、新たに必要になる操作・資源。
4. source互換、動作、callback context、buffer寿命、通信相手への影響。
5. 採用した実験条件・結果・判定。破壊する場合の移行例。

短い返信の例。現状は最後のflushを忘れると返信が出ない。

```cpp
// 現行
vendor.write(reply, length);
vendor.flush();

// 候補: copy writeの普通の使い方だけで送出が進む
vendor.write(reply, length);
```

ただし、writeのpartial戻り値を無視してよいという意味ではない。実例では戻り値を処理する。
採用するなら旧コードの明示flushが余分に残った場合も調べ、不要なZLPや二重送信を発生させない。

## 実装の順序

1. `validation/s3/vendor_roundtrip`で通常のS3–S3接続と短い返信を確認する。
   これは旧APIでも動く測定の入口であり、旧sampleを新APIへ合わせる作業ではない。
2. 自動送出と明示batchの候補を小さく実装し、上表1の利点と不利益を確認する。
   callback/転送完了から再armする場合の競合も確認する。
3. Vendorを新Function境界へ切り出し、容量設定と開始/終了を実装する。
   必要なregistry/allocatorだけを同時に作る。E1/E2/E3を新しい試験として追加する。
4. copy/borrowed切替、返却通知、複数instanceを実証し、CDCへ同じ意味を展開する。
5. HID/Audioとの複合を追加し、そこで共通化が過剰か不足かを見直す。

この順序は全置換の最終目標を変えない。P4/S2/S31、五台ベンチ、OS互換は後段の計画として残す。
最初のAPI採用をそれらの実機待ちにはしないが、S3で成立しただけで全SoC対応を宣言しない。

## 採用記録: Vendor writeの自動送出

2026-10-26。copy writeで受け入れたbyteがあれば送出を予約する方式を採用した。
S3–S3新規probeで1/63/64/65/127/128/129 byteを明示flushなしで往復し、
1/64/129 byteを1または17 byteずつwriteする場合と、各write後に既存のflushを加える場合も確認した。
計19条件、1 passed、39.84秒。結果は`validation/.pytest-results/vendor-auto-write.xml`。
writeの戻り値・buffer寿命・callback contextは維持し、flushは完了待ちには変えていない。
短いwriteごとの転送予約が増える可能性は残る。まとめ書き専用APIは必要性を実測してから追加する。
現時点の試験は転送速度やP4/S2/S31の動作を証明するものではない。

## 採用記録: instance資源と借用転送

Vendorクラスdriverを独自実装へ切り替え、容量・外部ring・autoFlush・RX modeをinstance configへ移した。
copy TX容量0やcallback RXでは未使用ring/stagingを確保しない。通常のPrint操作は維持した。
直接送信は同じbinaryで使え、status付き完了でbufferを返す。既存のbyte数だけの通知を返却保証には使わない。
設定とcallbackの稼働中変更、二度目のbeginは明示拒否し、設定値を保持する。
利点は再build不要、資源削減、寿命の明確化。不利益は開始時の確保失敗とbuffer寿命管理、
callback登録時期の制限、暗黙ZLPに依存したHostの移行である。

S3–S3の新規試験で86往復、batch受信、100回開始終了、外部ring canary、複数instance、
借用TXの16件の返却（成功14件、reset 1件、end 1件）を確認した。詳細な契約・移行・未実装範囲は[V3_VENDOR_API.ja.md](V3_VENDOR_API.ja.md)。
P4/S2/S31および上限長の成功転送はこの結果から対応済みとしない。

## 採用記録: 投稿式RXと投稿結果

RX modeにBorrowedを追加し、内部ring/packet bufferを使わずアプリがDMA領域を投稿する。
MPS/64 byte単位の容量制約と、完了まで領域を専有する負担が増える一方、不要なcopyとbuffer確保を避けられる。
投稿結果を値で返すEspUsbTransferSubmissionに変更し、別taskやTX/RXがlastErrorを上書きする問題を避けた。
TXの完了callbackは方向を明示する名前へ変更した。移行方法はVendorガイドに記載。
S3で成功13、部分reset 1、部分end 1の返却、callback再投稿とcanary、借用専用構成の100回開始終了を確認した。
P4 cacheと個別cancelは次の検証であり、今回の合格には含めない。

## 採用記録: 借用転送の個別取消し

取消し要求とbuffer返却を分離し、既存のstatus付き完了callbackへCancelledを追加した。
submission/resultへidを追加し、古い要求が次の借用を取り消さないようにする。
普通のcopy writeの利用コードにはid管理を求めない。
利点は接続を維持したまま不要な借用を終了できることと、callbackでの安全な再投稿。
不利益は取消し受理後も返却を待つ必要があること、借用ごとのidとpending状態の保持である。
S3–S3で20反復、部分転送、即再投稿、旧完了16件、queue満杯、古いidの拒否を確認した。
取消しと実bus reset/endの競合も3条件各3回で返却・再接続を確認した。
P4/S2/S31は未検証。詳細・結果はVendorガイドと引き渡し文書を参照。

## 採用記録: controller能力による内部DMA分割

S3実機で転送上限付近を流し、register型のpacket数幅10 bitと実装幅7 bitの相違によるtimeoutを再現した。
GHWCFG3の能力に合わせてDCD内のDMA armを分割し、bufferの借用・返却は全体で一回に保つ。
これにより利用者がS3の8128 byte単位を意識せず、大きなborrowed転送を投稿できる。
中間armはpacket境界に揃え、short/ZLPを挿入しない。
不利益はchunkごとのIRQ/cache処理と、暫定DCDのendpoint状態に進行量/chunk長各16 bitを保持する固定費である。
TX上限照会は65535 byteとなった。RXはMPS/cache専有容量単位で65472 byte。
既存の上限以下の投稿と通常Printの利用コードは変更不要だが、固定上限を決め打ちした利用は照会値を使う。
この16 bit上限はTinyUSB adapterの暫定制約であり、独自core/DCDで残す最終仕様ではない。

この修正後、標準入口のS3–S3全4 testsが成功（230.70秒）。
大きな転送のcaseで53件の返却、他caseで旧完了event/queue満杯、reset/end競合、通常copy/borrowed経路も確認した。

## 採用記録: CDCとVendorのbulk stream共通化

`EspUsbDeviceBulkStream`へ容量、外部ring、copy/borrowed、返却、取消しを切り出した。
CDCはこの経路と固有のline coding/controlを使い、TinyUSB CDC classと固定instance配列を廃止した。
通常のDevice+Function、Print/read/writeの利用感は維持する。
共通設定は`EspUsbBulkStreamConfig`/`EspUsbBulkRxMode`とし、Vendor名はaliasで継続可能。

コピー送信の終端を`EspUsbStreamTermination`で明示した。FunctionDefaultではVendorのContinueと
CDCのshort/ZLPをそれぞれ維持する。利点はHost側readを考えた既定動作と連続streamの選択、
不利益は要求設定のFunctionDefaultと実効policyを区別する必要、および選択した場合の追加ZLPである。
借用転送の明示終端とは別にし、write一回をメッセージ境界とはしない。

CDCのcallback/setNameは停止中登録に統一し、lineCoding/lineStateは参照からsnapshotへ変更した。
競合を避ける利点があり、取得済み参照の自動更新に依存する利用は再取得へ移す。
独自化後の個数は構成全体のendpoint予算で検証するため、compile時個数のmaxCdcPortsは廃止した。
S3で2 CDCとCDC+Vendor+HIDの構成を分け、容量変更、line coding、借用、通信、key押下/解放を確認した。
移行・制約・実験は[V3_CDC_API.ja.md](V3_CDC_API.ja.md)を参照。

最終の標準入口は6 passed、340.68秒。46取得点の失敗/回復とVendorの明示コピー終端も確認。
3 build probeのnmでFunctionごとのリンク除外と未使用取消し処理の不在を確認した。
RXを使用しない構成の明示mode、固定registry/descriptor領域の撤去は継続する。

## 採用記録: 明示的なRX Disabled

送信専用構成でRXの確保を避けるためにdummyの借用RX callbackを登録する必要をなくした。
Disabledを追加し、RX ring/packetを省略、OUTは未arm、受信APIは非対応として扱う。
modeが一つ増える一方、使わない受信を借用受信と偽って設定しなくてよくなる。
S3でCDC/Vendorそれぞれcopy/borrowed送信を確認し、RX callbackなしで開始・受信API拒否を検証した。
追加modeを含む確保失敗は54取得点で回復した。

## 採用記録: Function登録と複合ID

固定6 slotを廃止し、constructorの登録linkとbegin時の実数分の一覧に分けた。
constructorでのOOMによる脱落を避け、一覧の確保失敗をbeginの結果で扱える。
Functionごとにlink/登録状態を持つ負担とbegin時の確保点が増える一方、Deviceの未使用6 slotは不要になる。
稼働中登録は明示拒否し、未登録Functionがあるままの次回開始も拒否する。停止後に再登録できる。

7個のcustom HIDで既存のreport ID重複を実測したため、構成内で一意なIDを割り当てる方式を採用した。
通常の送信は自動で追従し、固定IDを使う通信相手にはdescriptorを読む移行が必要になる。
configuration descriptor自体の固定容量は残っており、今回の変更だけで全体の動的化が完了したとはしない。
詳細・実験は[V3_FUNCTION_REGISTRY.ja.md](V3_FUNCTION_REGISTRY.ja.md)を参照。

未登録のHIDからの送信は無効なruntime identityで拒否する。
登録拒否時に初期値slot 0から別Functionとして送ってしまう経路を残さない。
拒否されたCDC/Vendorの破棄がDevice全体を停止する問題も、所有する転送状態の有無で判定して修正した。

## 採用記録: descriptorの実数分の確保

固定configuration 704 / 複合HID report 256 / BOS・MS OS領域を撤去した。
Functionのサイズ照会→実数分の確保→容量付き出力に統一し、FS/HSも独立して構築する。
通常のDevice/Print利用は変えず、独自writerのnull照会契約だけは移行が必要となる。
利点は未使用領域の省略、長い複合構成、FS専用時のHS領域省略。
不利益はbegin時の計数・確保点と、独自writerへの対応である。

三つ以上のVendorでもMS OS setに全interfaceを含める。複数対象のFlat明示は拒否し、Autoではsubsetsへ進む。
S3で846 byteのconfiguration、516 byteのMS set、366 byteのHID reportを実際に列挙・通信した。
FS/HSのwriter44条件と86取得点の確保失敗/回復も含む標準全8試験が成功（390.92秒）。
契約・移行・残存固定領域は[V3_DESCRIPTOR_STORAGE.ja.md](V3_DESCRIPTOR_STORAGE.ja.md)に記録した。

## 採用記録: UTF-8文字列とURLの構成時snapshot

既存のconfigとFunction名の入力を維持し、開始時にUTF-16文字列を必要量だけ保存する。
63 byteの切り詰めとUTF-8のbyte単位の拡張を廃止し、規格上の126 UTF-16 unitまで受け付ける。
WebUSB URLも有効時だけ実数分を保持する。元配列の変更で稼働中のdescriptorが変わらなくなる。

変換・確保がbeginへ増える一方、利用者はビルド定数やUSB要求中の文字列寿命を管理しなくてよい。
不正UTF-8と長さ超過は明示的な開始エラーとするため、切り詰めや文字化けを前提とした設定は修正が必要。
WebUSBのprefixなしURLを暗黙のhttpsへ変えず、scheme 255で全文を保持する。httpsが必要なら明記する。
URLなしでWebUSBを有効にした場合はlanding pageなしとし、存在しないindexを広告しない。
契約と実験は[V3_TEXT_DESCRIPTORS.ja.md](V3_TEXT_DESCRIPTORS.ja.md)を参照する。

## 採用記録: HID classの独自化とdescriptor由来の容量

TinyUSB HID classと三つの固定bufferを外し、選択したHIDだけの独自driverへ変更した。
Report Size/Count/IDを解析してInput/Output/Featureの実効容量を決め、INのみならOUT/controlを省略する。
複合HIDは一つのstateを共有し、通常のKeyboard/Mouse等の送信方法と自動ID割当を維持する。

HidVendorのサイズをビルド定数へ結び付けず、FSの複数packetでも長いreportを送受信する。
長すぎるsendInputは黙って切らず拒否する。独自HIDの基底はEspUsbHidFunctionへ移行する必要がある。
開始時の解析・確保は増えるが、未使用時の固定領域とHID driver自体のリンクを避けられる。
S3の12サイズの実送受信とboot/NKROを確認した。GET_REPORT snapshotとidle自動再送は後続課題。
契約・移行・検証範囲は[V3_HID_API.ja.md](V3_HID_API.ja.md)に記録した。

## 採用記録: HIDの状態保存、idle再送、停止中設定

GET_REPORTを定義済みID/typeのsnapshotから返し、idleが有効ならInputをID別の周期で自動再送する。
保存領域は実report長の合計と、必要なrelative maskだけの一括確保にする。
制御応答用copyにより、別taskのFeature更新中も一つの応答内で値を混在させない。
FeatureはsetFeatureReportでアプリから更新でき、bus reset越しに保持する。Input/Outputとidleはreset時に初期化する。

Mouse等のrelative Inputは送信完了後にzeroへ戻し、idleで移動量を繰り返さない。
boot mouseの3 byte形式も扱い、短すぎるboot descriptorはbeginで拒否する。
Keyboard/Vendor HIDのcallback登録とNKRO設定をesp_err_t返却の停止中APIに揃え、
稼働中の無言変更とcallback競合を避ける。通常の呼出しの形は維持できるが、void型のmember pointerは移行が必要。

不利益はGET_REPORT/idleを支える保存・応答領域分のRAMである。定義しないOutput/Featureの領域は確保しない。
2 IDの独立状態、200回の並行読出し、20/100 ms指定の周期、停止とreset、boot/NKRO/MouseをS3で確認した。
実測と未検証の範囲は[V3_HID_API.ja.md](V3_HID_API.ja.md)を参照する。


## 採用記録: MIDIをevent単位のruntime構成へ

readPacket/writePacketとnote等のhelperを維持し、cable数、event queue容量、転送単位、
外部ring、Queue/Callback/Disabledを停止中APIへ移した。TinyUSB MIDI classと固定FIFOは無効にする。
共通bulkを使う複数instanceにし、4 byte eventの検証とcopy受理を同じlock内で行う。
MPS未満のringも一つのDMA packetに残りを保持して取りこぼさず読み出せるようにした。

利点は再ビルドなしの構成変更、不要方向の大きな領域省略、typed callback、複数MIDIと他Functionの併用。
不利益はbegin時の確保、小さいringでの小分けcopy、availableがring容量を超える場合があること。
不正cable数をclampせず拒否し、予約CINと範囲外cableのeventを排除するため、従来これらを送った利用者は修正が必要。
TinyUSB直接呼出しとCFG_TUD_MIDI_*設定もpacket API/configへ移行する。
HS/他SoC/OS音楽アプリの相互接続とMIDI 2.0は今回の検証範囲外。
実通信、並行終了、313取得点の失敗回復と移行は[MIDIガイド](V3_MIDI_API.ja.md)を参照する。


## 採用記録: HID protocol変更時の未完IN排出

boot/reportの形式変更時は旧INをhardware停止し、旧完了eventのFIFO fenceが通過するまで
明示送信/idleのbuffer再利用を止める。既存worker jobを共用し、新しいheap/taskは増やさない。
利点はHostが旧reportをまだ読んでいなくても、切替後に旧形式を渡さず送信を再開できること。
不利益はprotocol変更で未完Inputを破棄することと、fence通過まで一時的に送信が待つことである。
Input snapshotは既存方針どおり新形式のneutralへ戻す。

番号なしboot-capable Inputは現在の形式の正確な長さを検証する。
Keyboard helperが形式選択した直後にHostが切り替えた場合も、誤形式を受理せずInvalidSizeで返す。
通常helperはそのまま使えるが、独自コードが短いboot-capable reportを直接送る場合は修正が必要。
実験の手順と結果は[HIDガイド](V3_HID_API.ja.md)へ記録する。


## 採用記録: MSCのruntime bufferと独立BOT

sector長と転送staging容量を分離し、一つのDMA bufferをcommand/data/statusで共有する。
Functionごとのgeometry/callback/bufferへ移し、TinyUSB MSCの固定bufferと単一instanceを撤去する。
read callbackだけでread-onlyを構成でき、media/writableの状態はatomicに更新する。
callback/inquiry setterは停止中のesp_err_t返却へ揃える。
利点は容量の再構成・複数instance・不要書込みcallbackの省略。不利益は動的確保と小分けcallback、
短い/0のI/O戻り値を同期失敗にする移行負担である。通常のRAM/SD/helper attach手順は維持する。
BOT13ケースとS3での実験・未確認条件は[MSCガイド](V3_MSC_API.ja.md)へ記録した。


MSCの最終OUTはMPS単位でarmし、storageへ渡す量とCSW residueをSCSIの処理済みbyteで管理する。
sector長をMPSへ切り上げず、Hostが最終packetに余分なbyteを含めても次sectorへ書かない。
511/512 byte sectorの各BOT13ケースと隣接sectorの保持を実測し、追加bufferなしの方式を採用した。
5最小build/nmと標準全14試験（772.40秒）が成功した。


## 採用記録: Audio event領域の任意化と構成の停止中変更

固定8件の配列をeventCapacityの実数分へ移し、0なら通知配列を省略する。
満杯なら古いeventを保って新しい通知をdropし、無効化時は意図した省略をdrop数に含めない。
multi-producerのslot競合を短いcritical sectionで防ぎ、通知経路では追加確保しない。
稼働中の容量/format/方向の変更を拒否し、未登録Audioがglobal FIFOへアクセスする経路も止める。
通常のDevice/stream/pollEvent手順は維持する。

利点は不要通知の配列省略・容量調整・明示的な開始エラー。不利益は有効時の動的確保と短いlock。
PCM側のTinyUSB class/FIFO/globalは残っており、Audio全置換の完了とは扱わない。
S3の実USB制御と413取得点の失敗回復は[Audioガイド](V3_AUDIO_API.ja.md)へ記録した。

## Audio PCMのruntime資源

2026-10-27。通知容量に続きTinyUSB Audio class/FIFOを独自driverへ置き換える。
基本のDevice + Function + stream/format + begin/read/writeは保ち、停止中configureで
方向別bufferFramesと任意の外部bufferを指定する。frame単位の容量は24 bit PCMでも
channel一組を分断しないためで、read/write/available/capacityの値は既存のbyte単位に揃える。

選択方向にだけringとselected formatの最大packet分のDMAを確保する。
容量0はringなしのPlayback破棄/Capture無音とし、転送を支えるDMAの費用は残る。
UAC2 Playbackだけfeedback領域を持つ。各Functionのstateを分け、単一global instanceを撤去する。
外部ringはCPU copy用とし、DMA alignmentを利用者に要求しない。

Captureは満杯時に未送信データを上書きせず、入ったbyte数を返す。
これは旧FIFOに比べデータ保持とbackpressureが明確になる一方、戻り値を無視した利用は移行が必要。
availableForWriteで空きを調べ、残りはアプリが再投稿する。既にarmしたpacketはclearBufferでは取り消さない。
alt変更はhardware abortと旧完了eventの排出後に再armする。最初の開始時だけ事前充填を保持する。

周期処理はまずworkerで検証する。S3実測の48 kHz/44.1 kHz stereo 16 bitでは
1.5秒のcaptureで287232/263892 byteを受信し、payload不正・無音補完は0だった。
現時点でISR経路を追加する根拠はなく、アプリcallback/borrowed PCMの契約も未導入とする。
この値は性能比較ではなく、周期転送が継続する実装を選ぶための確認である。
公称clock feedbackのみで外部codec clockの追従は扱わず、複数formatと固定control/model配列も後続へ残す。

## DFU転送サイズと保存先

2026-10-27。DFUを次の置換対象とする。旧transferSizeはstaticでCFG_TUD_DFU_XFER_BUFSIZEに固定され、
RuntimeだけでもDownloadのbufferをリンクし、単一global instanceが全interfaceを制限していた。
EspUsbDfuConfigで最大blockと外部bufferを選び、Runtimeはblock領域を持たない方式へ変更する。
通常のDevice + Dfu + beginは維持し、transferSizeをinstance getterへ、onErrorを型付きstatusへ変更する。
稼働中setterを拒否し、宣言した容量と状態機械が食い違う変更を防ぐ。

USB protocolとOTAを分ける同期backendも用意する。特定アプリ向けではなく、外部flashや別processorへ
imageを渡す用途を含める。byte配列の保持はcallback中まで、途中書込みはstatusで失敗、manifestで検証・適用し、
失敗/中断時にはabortで片付ける。既定OTAのComplete vetoと自動再起動はcustom backendへ持ち込まない。
利点は保存先の自由と実state machineのRAM backend検証。不利益はcallback契約と開始失敗、callback自体の費用である。
非同期flashやUploadの先行設計はしない。まずS3上でGETSTATUSと実control転送の結果を確認する。


## 補助文字列をFunction名から分ける

2026-10-27。NCMのMACはinterface名と用途が異なるため、functionNameをMACとして流用しない。
ClassのauxiliaryStringCount/auxiliaryStringで宣言し、assignAuxiliaryStringIdsで割当てを受ける。
名前の既存indexを変えず、その後の空間を使う。範囲とUTF-8を検証し、共通のUTF-16領域へcopyする。
利点は複数NCMを同じglobal MAC indexへ向けずに済むことと、他class固有の文字列にも使えること。
不利益は派生Functionに3つの任意virtual hookとbegin中の安定性契約が加わることである。
不要時の追加heap/instance fieldは持たず、begin時のvirtual呼出しは残る。
MACの実際の移行はNCM driverの独自化と一緒に行い、現段階のlegacy通信制約を隠さない。

## NCMのNTBとIP層をFunctionへ移す

2026-10-27。固定NTB pool、単一MAC、1600 byte × 4件の永続送信queueを撤去し、
EspUsbNetConfigでNTB容量・方向別buffer数・datagram数・frame上限・外部DMA領域を選ぶ。
buffer数0の方向はpoolを省略する。末尾ZLP、Hostの容量再交渉とalt変更のDMA停止/排出を独自driverで扱う。
設定では単位を分け、byteはntbIn/OutCapacity、bufferの数はtx/rxBufferCountへ揃える。

sendFrameはNTBへcopyしてworkerへ渡し、満杯時に待たずfalseを返す。
利点は中間frame queueのcopyと永続確保の削減、TCP/IP taskからDMAを触らないこと。
不利益は配送完了通知を持たないbest effortと、再交渉/alt変更で送信待ちframeを捨てる点。
NTB容量変更ではDATA PIDを保持し、SET_INTERFACEではDATA0へ戻す必要があるため、両者を区別する。

MACは補助文字列を使ってFunctionごとにsnapshotする。IPは別ファイルのbeginNetworkからだけ導入し、
raw transportにはesp_netif glueをリンクしない。netif/key/IPv4情報を分離し、endNetworkで破棄して再開始できる。
IP用frame copyはlwIPの寿命に合わせて残し、IPを選んだ場合だけ費用を持たせる。
基本のDevice/Net/onFrame/sendFrame/beginは維持し、setterはesp_err_t、稼働中は明示拒否へ変更する。


## Video設定の検証と稼働中破棄

2026-10-27。Video独自化の前に、active Function破棄がDeviceを停止せずframeの寿命を切っていた経路を修正する。
他Functionと同じDevice停止に揃え、prepareToStopで次のframe投稿を拒否する。
descriptorの32 bit bit rateがoverflowした値を送らず、setterの候補全体を検証して旧値を保つ。
MJPEGにYUY2の幅制約を持ち込まず、fpsの上限を255とwireのbit rate上限で決める。
利点は破棄後のDMA参照の防止と不正descriptorの早期拒否、不要なサイズ/fps制約の削減。
不利益は稼働中破棄がDevice全体を停止し、不正な上限申告を従来どおり受け付けなくなる点。
payload容量やtransfer方式はまだbuild flagなので、これだけをVideoの独自化完了とはしない。


## Videoのpayload/方式をFunction設定へ移す

2026-10-27。Videoの単一global ownerと固定stagingを撤去し、EspUsbVideoConfigで
ISO/Bulk、payload容量、ISO packet、任意の外部DMA領域をFunctionごとに設定する。
下位EP0/core/DCDはまだTinyUSBだが、Video classは独自app driverで扱い、legacy flagを拒否する。
利点は同一binaryでの構成変更、選択Functionだけの資源確保、共通の借用frame返却契約。
不利益はbegin時の確保失敗、外部DMA spanと借用frameの寿命管理、static照会APIの変更である。

sendFrameと成功専用onFrameCompleteの操作は維持する。submitFrameのticketとcancelFrame、
onFrameResultを加え、中断/reset/停止でも借用bufferを返す。callback登録は開始後に変更できない。
取消しとalt/recommitではDMA停止後のevent fenceを通し、古い完了が次frameを返すことを防ぐ。
Probe/Commitは広告した一形式・一固定frame intervalの範囲で受け付け、payload上限を実資源に合わせる。
複数formatや可変intervalを未実装のままAPIで約束しない。詳細と移行例は[Videoガイド](V3_VIDEO_API.ja.md)。


## 共通coreを独自化し、event容量を開始時設定へ移す

TinyUSB usbd.cと未使用のbootstrap/FIFO実装を除去する。EP0の状態とevent世代を独自coreで持ち、
selected interface/endpoint map、queue、64-byte EP0 DMA領域を一つのbegin時領域へ集める。
eventQueueDepthはuint16_tの非ゼロ値で、確保できない構成はNoMem。任意の4096等の上限は設けない。
利点は固定global領域と旧core依存の撤去、同一binaryの容量変更、旧SETUP/data混入の防止である。
不利益は新しい開始時確保失敗と、queue過負荷時に接続を回復するため通信を中断する点。

sendControlResponseの0-byte応答は、要求長0のstatusと、非ゼロIN要求への空dataを区別する。
従来のように常にstatusへ飛ばすとHostがdataを待ち続けるため、後者はZLP dataを送る。
空の返信が自然に使える利点があり、OUT要求を受信せずACKしていたコードは拒否へ変わる。
通常操作は維持し、内部class/DWC2のC ABIとstartTinyUsb等の名前は残存依存の後に整理する。
全標準要求・PID/所有権遷移の完成を今回の関連試験だけで主張しない。残りと実測は[共通coreガイド](V3_CORE_API.ja.md)。


## Vendor/CDCの標準要求とcopy専用構成の費用

2026-10-27。SET_INTERFACEをACKするだけでは借用が返らず、HALT解除後の再開にもPIDの不整合が残る。
共有bulk transportで対象interface/endpointのDMA停止、既存fence、typed callback返却を扱う。
APIの呼出し形は維持し、SET_INTERFACEはCancelled、HALTはTransferError、CLEAR_FEATUREはCancelledとする。
既に保留中のTransferErrorや競合するBusReset/Stoppedを弱い結果で上書きしない。

利点はHostによる再初期化でもbuffer寿命が決まり、無関係なFunction・方向の通信を保てること。
不利益は対象方向のcopy queue/ringを破棄すること、HALT中のcopy writeが0になること、
copy専用でも必須回復用abort/fenceコードが入ること。追加の転送時heap確保はない。
copy最小buildのnmで必須回復の存在、公開cancelDirectTx/Rxと未使用CDC/Audio/Video等の不在を確認した。

queue depth 1ではfenceとEP0 statusが競合するため、指定深さにhardware event用1枠を追加する。
小さな固定追加費用で、正常な標準要求が意図しない再列挙を起こす条件を避ける。
物理queue全体の過負荷時には既存のDMA停止・返却・自動再列挙を維持する。
試験は独立bulk_standardを追加し、他Function/方向のPID継続、部分転送、copy回復も確認する。
Host側のpipe再開が旧PIDを維持する条件では、両方向の初期化後にHost pipeだけを再生成した。
Deviceへ余分なSET_INTERFACEを送って回復を隠す方法は用いていない。


## CDC設定の受信実長

SET_LINE_CODINGで要求長だけを検査すると、short OUTで以前のbufferの末尾を含む値を受理し得る。
DATA段階で実長7 byteを要求する。公開API、追加heap、通知の正常経路は変えない。
不正な短い設定を従来受理していたHostにはSTALLとなるが、不完全な値を採用しないことを優先する。
要求長・不正設定値の拒否と設定/callback維持はS3–S3で検証した。
Host SDKが送信量とwLengthの不一致を拒否するため、物理short OUTの実測は残課題とする。


## Vendor control実量をcallbackへ渡す

HostのwLengthと実受信量は同じとは限らず、アプリが内部関数を呼ぶ必要があった。
EspUsbDeviceVendorControlRequestにactualBytesを追加し、SETUP=0、DATA/ACK=実data量とする。
INの切り詰め/short/ZLPも同じ定義にし、方向ごとに別の問い合わせAPIを増やさない。
利点はcallbackの値だけで長さを検証でき、TinyUSB由来のprivate headerを利用側から外せること。
追加heapはなく、代償はcallback構造体の拡大と位置指定aggregate初期化への影響である。
通常のcallback登録とsendControlResponseの呼出しは維持する。


## HIDの標準要求と内部buffer寿命

SET_INTERFACEの共通alt 0 ACKだけでは、送信中フラグとOUT組立て途中の状態が残る。
HID内でDMA停止と方向別fenceを扱い、SET_INTERFACEは両方向、HALT/CLEAR_FEATUREは対象方向を回復する。
既存SET_PROTOCOLのIN専用drainも同じjobへ整理し、format変更時はPID維持、標準再初期化ではDATA0と区別する。
利点はHostの再初期化後に送受信を再開でき、他方向の部分転送を保てること。
不利益は受理済みの未送信copyと受信途中レポートが破棄されること、HID選択時の回復コード・状態の増加。
追加task/転送時heapはなく、未使用HIDにこの処理は持ち込まない。
公開送信APIはcopy受理のままとし、借用buffer返却callbackを新設する変更は含めない。


## MSC interface再選択をBOT resetと区別する

共通alt 0 ACKだけではMSCの共有DMAとCommand/Data/Statusの状態が残るため、
MSC自身のepoch付きrecovery fenceへSET_INTERFACEを接続する。
通常の未完コマンドを破棄して次のCBWへ戻す一方、不正CBW/報告済みPhase ErrorのBOT reset要求は保持する。
BOT reset中のinterface再選択も、未受信のendpoint CLEAR_FEATUREを完了扱いにしない。
利点は中断後に古いCSWやdataを次のコマンドとして扱わず、必要なBOT回復手順を維持すること。
不利益は進行中コマンドの破棄と、部分的にstorageへ反映済みのWRITEをアプリが確認する必要があること。
追加task/転送時heapと公開API変更はない。書込みの原子性やロールバック保証を新設するものではない。


## CCIDの容量・instance・通知をruntimeへ移す

ESP_USB_DEVICE_CCID_BUFFER_SIZE、単一global ownerとdriver state、Function内の固定返信配列を撤去する。
EspUsbCcidConfig.messageBytesはheaderを含む全message容量で、descriptorにも同じ値を出す。
既定320、64～65535 byte。実際のcontrollerの最大packetより小さければbeginで拒否する。
2つのCCIDを別interfaceとして登録でき、各状態とbufferをbegin時に確保しendで返す。
返信stagingと送信bufferの重複を避け、1本のDMA返信bufferをIN/ZLP完了まで保持する。
次のOUTをその後にarmし、1 slot / 1 busy commandという広告とbackpressureを揃える。

removable=falseはカード固定を表す。開始前のinsertCardを必須とし、稼働中のinsert/removeを拒否して、
通知endpointとbufferを省く。単に通知だけを無効化して着脱可能とする設定は追加しない。
[USB-IF CCID 1.1](https://www.usb.org/sites/default/files/DWG_Smart-Card_CCID_Rev110.pdf)の
§3.1.2、§5.1、§5.2を基準に、着脱可能なreaderの通知要件とmessage容量を扱う。

利点は構成ごとの資源、複数readerの独立性、共有返信bufferの寿命が決まること。
不利益はbegin時の確保失敗、返信完了までの次command待ち、稼働中設定変更の拒否。
onApdu/onEscape/onPowerはesp_err_tを返して開始後の変更を拒否し、removeCardも結果を返す。
通常の登録呼出しは戻り値を無視していた形を維持できる。容量指定はconfigureへ移す。
ATRの33 byte配列はwire上限であり、選んだFunction内に残す。交換時のcopyとPowerOnのsnapshotはlockで保護する。
大きいmessage容量をextended APDU対応の広告へ自動変換しない。大きいEscape等の資源指定とprotocol機能は分ける。


## DWC2 stateの所有者を独自coreへ移す

上位class/coreだけでなく、残るDWC2のstatic xfer_statusとcontroller/SETUP領域もruntimeへ移す。
使うendpoint方向だけのcompact recordをcoreの開始時領域へ含め、停止後にまとめて返す。
未使用方向のrecordは作らず、全alternateで必要なendpointを先に集める。hardware全体の停止処理は省かない。
利点は未起動時の常駐DMA/state費用削減、controller選択と寿命の一体化、別heap取得点が増えないこと。
不利益はbegin時の容量増加、maskからslotを求めるlookup費用、開始時確保失敗の対象拡大。
公開API、FIFO容量やDMA segmentation、callback/fence契約は変えない。
残るvendor sourceはMIT由来とpatchを維持し、独自DCDへの置換が完了したという扱いにはしない。


## DWC2 SETUP DMAの初回cache同期

runtime化で初期化したSETUP cache lineをhardwareへ渡す前にclean/invalidateし、
遅れたCPU writebackが受信済みSETUPを壊す条件を避ける。成功するまでruntime pointerを公開しない。
同期失敗はESP_FAILとし、領域を返して再試行可能にする。NoMemや設定値不正とは区別する。
公開設定は追加せず、既存backend hookへ開始時だけ2回の呼出しを加える。
cacheのないtargetの処理はno-op。heap取得点と通常の転送経路は増やさない。
S3の失敗注入とP4 compileを、P4の実cache coherency試験と同一視しない。


## 構成世代による完了eventの隔離

SET_CONFIGURATIONの再選択でendpoint番号を再利用した際、古いqueue内完了が新規借用を返してしまう条件を実機で再現した。
bus/SETUP世代に加えて構成世代を持ち、class全体をresetした前のnon-control完了を状態変更前に捨てる。
EP0とdeferred fenceは別の寿命契約を維持する。
利点は新しいbufferをHost data完了より早く返さず、古い結果で新しいendpoint flagsを解除しないこと。
不利益はevent一つにつきuint32_t一つとcore側世代の開始時領域増加。公開API、転送時heap取得は変えない。
構成世代は投入済みqueueの識別であり、全ISR順序やarm tokenの代わりという扱いにはしない。


## 構成世代とhardware完了の境界

構成世代を導入した後も、latched XFRCが新規armの状態で処理される条件をS3で再現した。
close_allで停止完了待ちと非制御DEPINT clearを行い、転送recordを初期化する。
endpoint ISRとtask操作は同じhardware lockで同期し、SOF class callbackはlock外へ保つ。
利点は再構成で返したbufferへ旧DMA/IRQ処理が残らず、旧完了を新規転送へ適用しないこと。
不利益は停止待ち時間とcritical sectionの増加。公開API、通常転送のheap取得点は変えない。
全てのISR順序をarm tokenで証明したという扱いにはせず、EP0/bus reset等の境界検証を継続する。

## native class登録表の独自化

独自coreは選択されたclassだけを`UsbClassDriver`として登録し、C++の
`selectedUsbClassDrivers`から借用する。TinyUSBの`usbd_class_driver_t` layoutと
`usbd_app_driver_get_cb` hookは使用しない。coreが呼ばないname/xfer_isr枠も除き、
32bit環境では登録tableが1 driverあたり8 byte小さくなる。取得点数や転送中heapは増やさない。
classの完了/controlはworker、SOFは従来どおりISR。登録/解除はcore停止中という契約を維持する。

native class用のendpoint宣言は`internal/EspUsbDeviceEndpoint.h`へ限定した。
classから`device/usbd_pvt.h`を直接includeせず、vendorのOS/FIFO/class tableを引き込まない。
coreと残存DWC2 adapterは引き続き同headerを使う。endpoint/controlのC symbol名、
wire typeやprotocol headerの依存は残り、全ABI撤去や全TinyUSB撤去とは区別する。

利用者APIと移行手順は変わらない。不利益は段階移行中のendpoint宣言が残存bridgeの宣言と
重なること。coreのtranslation unitで両headerをincludeし、型の不一致をcompile errorにする。
両方の意味を変えるときはまとめて変更する。driverの任意ISR dispatchは公開しない。

## controller境界と未使用のvendor基盤の撤去

`internal/EspUsbController.h`がcoreと残存DWC2 adapterの専用境界となる。
型付き`EspUsbControllerConfig`でbulk INの二重bufferとVBUS policyを渡し、initはdevice専用の
port/highSpeedを受け取る。旧DCD symbolとvendor config ID/unionは使わない。
controller eventは独自型とし、deferred fenceはcore内の予約ID 255で区別する。
controller入口は無効/範囲外eventを拒否し、worker内のjobだけがdeferred payloadを使う。
control ACK observerも独自coreから専用hookへ通知する。

使われない`usbd_*`互換entry、vendor DCD/private、OS/FIFO、umbrella headerを撤去した。
ESPは従来同様internal DMAで動かし、無効だったslave/FIFO経路とrecordのFIFO pointerを削除する。
controllerにinternal DMAがなければ初期化を明示的に拒否する。新SoCの非DMA経路を追加する場合は
専用実装が必要だが、今回対応するS2/S3/P4の転送方式は変えない。
転送時のheapや確保点数は増やさず、recordから32bit pointer一つを減らす。
最終storage量は既存の64byte丸めとdescriptor選択数に従い、必ず4byte単位で減るとは限らない。

利用者API/移行手順は変わらない。内部TinyUSB APIへ直接依存する非公開利用は対象外。
故障注入probeは独自header/symbolへ変更し、queue深さ・cache失敗・旧完了/latched IRQの条件を保つ。
型や名前を変更しただけでDWC2が独自実装になったとは扱わない。MIT由来は維持し、
vendor選択は24→17 files、compiled sourceは2本のまま。wire/protocol定義、device向け残存headerと
DWC2 register/port処理の置換は続く。
