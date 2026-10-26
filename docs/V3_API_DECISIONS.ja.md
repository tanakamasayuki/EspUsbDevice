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
