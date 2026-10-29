# CH32RVとの利用者API調整メモ

2026-10-30追記：末尾にCH32RV側からの6点の再検討を記録した。
以下の10月28日比較は当時のsnapshotであり、現在のCH32RV側の未対応一覧ではない。
再検討は80e8676を基準とする判断案。実装の進捗は末尾の適用記録を参照。

2026-10-28、ローカル`ArduinoCore-CH32RV/libraries/CH32RVUsbDevice/src/`の公開headerを比較。
当該libraryの直近commitは`d99f8979`。これは移植計画でも、CH32RV側を変更した記録でもない。
ESP側のv3を実測で固め、共通にできる操作・契約をCH32RV側へ共有するためのメモである。
他機種の制約を共通APIの定数へ持ち込まず、名前だけで互換と判断しない。

| 項目 | 現状の差 | v3の判断・調整案 |
| --- | --- | --- |
| DeviceとFunction | 両方ともDeviceにFunctionを関連付ける。CH32RVには既定global Deviceもある | 明示的な関連付けを維持。globalや自動起動をESP側の必須経路にしない |
| begin再呼出し | CH32RVは成功後の再beginを成功扱い。ESPはInvalidStateで拒否 | ESPの拒否を維持。異なるconfigが適用されたと誤解させず、end→configure→beginを共通の再設定手順にする |
| endと所有権 | CH32RVはvoid、ESPはesp_err_t。ESP workerからのendは拒否される | 戻り値より「成功した停止後だけbuffer/Functionを破棄できる」を揃える。ESPの失敗をvoidで隠す互換化はしない |
| started/ready | CH32RVにもstarted/readyがあり、readyはmountedの別名 | ESPもstartedは資源所有、readyはHost構成済みとした。reset時にready=falseでもstartedはtrue |
| 容量設定 | CH32RV CDCはsetBuffers、Vendor容量はstatic。ESPは共通bulk configureとinstance実効容量 | 共有候補は要求容量・任意外部領域・実効容量の区別。既定容量やDMA alignmentまで同じ数値にしない |
| copy write | CH32RV CDCはHostが開いていれば満杯待ち、閉じていればdrop。ESPは受理数を返すpartial/nonblocking | ESPのpartialを維持。移行側が残りを保持し、速度差で無言の欠落やUSB workerのblockを作らない |
| CDCの型 | 両方ともArduino Stream派生。ESPは共通転送層からPrintを分離してCDCだけStream化 | Stream参照・peek・readBytesを直接使える。ESPのcopy writeはpartial、flushは送出開始のままで、待ちの違いは残る |
| 空き容量と待ち | CH32RV CDCはavailableForWrite、VendorはwriteAvailable。waitWritableは容量超過をclamp | ESPもPrint::availableForWriteを実効空きへ合わせる。旧動作の0は実機で再現した。size_tのwriteAvailableは共通bulkの照会として残す。waitWritableの要求超過は拒否する。名前の統一だけで待ち・clamp・dropの差を隠さない |
| 通知context | CH32RVは主にdeliverEvents、ESPの転送完了はUSB worker | 共通名でも同じ実行contextではない。block/end禁止と次投稿可否を契約へ明記する。cooperative task APIをESPへno-opで残さない |
| borrowed TX | CH32RVはbool＋lastDirectError、onTxCompleteへ完了を通知。ESPは投稿結果/idと型付き返却 | ESPの所有権返却を維持。reset/stop/cancel/errorでも一度返す契約を共有候補にする。alignment違いのために隠れcopyを入れない |
| control callback | CH32RVの公開requestにはactualBytesがない。ESPはstageと実data量を公開 | stageごとの意味と実量を合わせる候補。rawRequestの内部pointerを保存して遅延返信できると約束しない |

比較した主なheaderは`CH32RVUsbDevice.h`、`CH32RVUsbDeviceVendor.h`、
`CH32RVUsbDeviceCdcSerial.h`。全クラス・全戻り値の互換表ではない。
ESP固有のworker/core/cache、CH32RV固有のclock/USBSS/cooperative実行は各platformへ残す。

基本例の宣言、configure、begin、read/writeという操作感を優先する。
CH32RV側の更新案を作る際は、容量切替・partial write・reset中の借用返却の例を両方で比較し、
利点と既存利用への影響を確認する。CH32RV実装やOEPへの追従をESP側の合否基準にはしない。

## System Controlのusage値

EspUsbDevice v3ではSystem Controlの指定を標準usage0x81/0x82/0x83へ揃える。
名前付き定数とpress/release/click/sendUsageの形は保ち、wire selector1/2/3へ変換する。
CH32RVの現行定数1/2/3と数値の意味は異なるので、共有アプリは各ライブラリの名前付き定数を使う。
CH32RV側の変更はこの作業で行わない。両者を合わせる場合の調整点として残す。
[HIDの判断と移行影響](V3_HID_API.ja.md#system-controlのusage指定)を参照。


## 2026-10-30：CH32RV側からの6点の再検討

入力資料は`ArduinoCore-CH32RV/docs/usb-espusbdevice-v3.ja.md`の3章。
資料とESP製品80e8676、CH32RV作業treeの関係箇所を照合した（確認時CH32RV HEAD `97bf0695`）。
この節は判断案と試験計画であり、製品の実装変更・追加実機試験の結果ではない。CH32RV側は変更しない。

| 項目 | 判断案 | 主な影響 |
| --- | --- | --- |
| CDC write/flush | CDCのArduino facadeを期限付き待ちへ見直す。共通bulkはnonblockingを維持 | 普通のprintの欠落を減らせる一方、満杯時に待ちが生じる |
| Audio playback overflow | live再生では古いsample frameを捨てる既定を推奨。必要なら保持方針を選択可能にする | 最新の音へ復帰できるが、未読順序と欠落箇所が変わる |
| Video既定transport | FS=約62 KB/sという理由だけでは変更しない。Bulkを勧める条件とpacket予算を説明 | ESPの既定ISO packetは512で、64-byte前提と異なる |
| Video submitFrame | EspUsbTransferSubmissionへ統一する案に賛成 | sendFrameの基本例は維持。詳細APIの利用側を移行する |
| DFU onDetach | ACK前の受付判定と、受付後の通知・再起動方針を分ける | voidをboolへ置き換えるだけではHostへの要求拒否にならない |
| MSC媒体交換 | UNIT ATTENTIONと媒体変更の追跡を追加する案に賛成 | SCSI/BOTとHost側の回復試験が必要 |

### 1. CDC：Arduinoの入口と転送エンジンを分ける

現行のCdcSerial::write/flushは共通bulkへの直接委譲。writeは即時partial、flushは送出開始だけである。
Core3.3.12のPrint実装も短いwrite結果を自動で再送しない。厳密には「printが戻り値を見ない」より、
「受理数を返すが残りを再試行しない」が正しい。普通のprintで満杯時に文字が欠け得る指摘は妥当。
Arduino公式はwriteを空きがある間は非同期・満杯時は待ち、flushを送信完了待ちとして説明する。
この入口については、10月28日のpartial維持という判断を見直す。
[公式write](https://github.com/arduino/reference-en/blob/master/Language/Functions/Communication/Serial/write.adoc)、
[公式flush](https://github.com/arduino/reference-en/blob/master/Language/Functions/Communication/Serial/flush.adoc)。

案はCDCだけにsetTxTimeoutMsを持たせ、期限0は即時partial、通常は期限まで残りを送る。
StreamのsetTimeoutは読出し用のまま。Vendor/MIDIや借用投稿まで一括で待ちへ変えない。
worker/ISRでは待たず、切断・停止で打ち切り、autoFlush=falseでも待つ前に送出を進める。
DTR必須化は別の判断であり、現在の「writeはDTRを強制しない」契約を紛れて変えない。
flushはring空だけでなくDMA中のdataと必要な終端ZLPまで待つ。Hostアプリの読出し完了とは別。
期限切れは有り得るので、無欠落を保証するAPIとはしない。

### 2. Audio：保存量と音の鮮度を区別する

現行はringの空きだけ追加し、入らない新着byteを捨てoverrunへ記録する。
どちらの方針でも保存量は容量以下だが、読出しが止まると古い音を残す方針では長時間前の内容が残り得る。
live再生の回復には古い方を捨てる方が自然。録音・検査用途の前半保持は別の方針として選択可能にする案。
捨てる単位はbyteでなく全channelを含むsample frame。packetがringより大きい場合も最新の入る分を残し、
破棄量をstatsへ数える。captureのwriteのpartial契約まで変えない。
どちらも欠落による音の不連続は生じ、clockずれやfeedbackの代用にはならない。

### 3. Video：ESPのFS ISOは既定64 byteではない

EspUsbVideoConfigのpayloadBytes=512、isochronousPacketBytes=0が既定で、
isochronousPacketSize(false)は512を返す。bulkのFS MPS64をISOへ流用していない。
2-byte UVC header込みで毎1 msに1 packetを送れる仮定なら、frame dataの理論値は510000 byte/s。
実測帯域やfpsの保証ではなく、規格上限FS1023とSoCのFIFO/複合時に確保できる容量も別である。
CH32RVの64-byte制約だけを理由にESPもBulkへ変える必要はない。
可変サイズMJPEGや予約帯域より転送量を優先する用途ではBulkを勧める。
既定変更はISOの帯域予約、Bulkの再送/遅延変動、FIFOとPC UVC受入れを比較して決める。
[Videoガイド](V3_VIDEO_API.ja.md)には既にBulk例と速度別packet設定がある。

### 4. Video：借用投稿の結果型を共通にする

現在はsubmitFrame(frame,length,id)がesp_err_tを返す。案はsubmitFrame(frame,length)を
Submission戻り値にしてwriteDirectと統一し、単純なsendFrameはboolのまま維持する。
未開始/未commit、Busy、不正なframe、内部投稿失敗を区別し、拒否はid=0・借用なし・返却通知なし。
Videoのframe自体にbulk DMA alignmentを新たに要求するわけではない。
受理後の一度だけの返却、cancel/reset/end/recommit fenceは既存契約を維持する。
内部submitも、複数の理由をESP_ERR_INVALID_STATEへまとめる現行判定から整理する必要がある。

### 5. DFU：再起動の抑止とDETACHの拒否は別

ESPはSETUPでstatus応答を予約し、ACK後にAppDetachへ進んでvoid callbackを呼ぶ。
未登録ならROM bootloader再起動を予約し、登録時はアプリへ移行処理を任せる。
CH32RVも確認時のusbControlはSETUPでcontrolStatus、ACKでdetachを呼び、その中でboolを評価する。
falseは再起動・DFU移行を止めるが、既に成功応答した要求をUSB上で拒否したことにはならない。

「書込み中なのでHostへ拒否を返す」なら、例としてSETUP時のcanDetach判定を受付後通知と分け、
拒否時はcontrol STALL・appIDLEのまま・再起動予約なしとする案を評価する。
判定はworkerで短く行い、長い保存処理を入れない。許可後の再起動予約はACK後とする。
自前handlerによる移行も明示的な方針として残し、onDetachの戻り値変更だけでまとめない。
この受付方針はライブラリ拡張案としてDFU状態遷移・bitWillDetach・Host toolとの整合を試験する。
[DFU 1.1仕様](https://www.usb.org/sites/default/files/DFU_1.1.pdf)を参照。

### 6. MSC：present flagだけでは媒体変更を通知できない

現行mediaPresentはatomicなpresent flagの変更だけで、senseや媒体変更のpending状態を持たない。
未挿入時のNOT READY/3aと、交換後のUNIT ATTENTION/28/00は別。
[T10 ASC/ASCQ一覧](https://www.t10.org/lists/asc-num.htm)で28/00は
NOT READY TO READY CHANGE, MEDIUM MAY HAVE CHANGEDとして定義される。
稼働中のfalse→trueを媒体世代の変更として追跡し、対象commandへ一度通知する案に賛成。
INQUIRY/REQUEST SENSEの扱いとpending消費条件を決め、同値のtrue設定だけで毎回交換扱いにしない。
同じpresentのまま内容を替える用途が必要なら明示通知APIを検討する。
I/O中の保存先を破棄してよいという意味にはせず、進行中のread/writeとの境界も定義する。
Hostのfilesystem cacheを必ず再読させる保証にはせず、unmount/ejectと媒体所有者の扱いも分ける。

### 実装の順序と合格条件

Video戻り値、CDC入口、Audio overflow、DFU受付、MSC媒体交換の順で小さく分ける案。
既定transportは用途/packet説明を補い、今回の根拠だけで変更しない。
S3–S3の独立validationを基準にし、conftestを追加しない。P4の両controller FSへ関連caseを展開する。

| 対象 | 必要な検査 |
| --- | --- |
| CDC | ringより長いprint、満杯/timeout0/通常期限、autoFlush=false、Host停止/切断/Device停止、worker内呼出し、DMA/ZLPまでのflush |
| Audio | 古い/新しいPCM、wrap、packetがringより大きい場合、sample frame境界、破棄stats、capture partial維持 |
| Video | error/id、sendFrame、完了/取消し/reset/end/recommitで一度返却、ISO/Bulk回帰 |
| DFU | 拒否のwire STALL/状態不変/予約なし、許可のACK→通知→予約、繰返し、custom handler、停止競合 |
| MSC | 挿抜→TUR失敗→REQUEST SENSE 06/28/00→再試行成功、INQUIRY例外、同値設定、NOT READY区別、I/O途中の交換、BOT回復 |

未使用Functionへtimeout state、追加ring、媒体監視task等を一律追加しない。
既存の77件の収集/各実行結果は現行製品の証拠であり、上記変更案の検証済み扱いにはしない。


### 適用記録

- Video submitFrameのSubmission戻り値を適用。sendFrameは維持し、旧3引数overloadは撤去。
  S3 8件＋P4 FS 4件の関連12件を一括成功（179.81秒）。[現行Video API](V3_VIDEO_API.ja.md)を参照。
- VideoのISO既定512 byteと、Bulkを勧める用途をAPIガイドへ追記。transportの既定自体は維持。
- CDCのwrite/flushへ期限付き待ちを適用。setTxTimeoutMs、timeout0、worker/停止の扱いは[CDCガイド](V3_CDC_API.ja.md#cdc送信の期限とflush)を参照。新規3件＋既存S3 3件＋P4 6件の関連12件が一括成功（374.48秒）。S2基本例もcompile成功。
- Audio/DFU/MSCは上記の判断案であり、適用済みではない。
