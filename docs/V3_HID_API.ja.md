# v3 HIDのruntime転送領域

2026-10-26。HID class driverを独自実装へ変更した。TinyUSBのHID class、固定instance配列、
IN/OUT/control bufferを使用しない。共通EP0/coreとDWC2も本体内部で保守する。

## 通常の利用

Keyboard/Mouse/Custom/Vendor/Gamepad/ConsumerControl/SystemControlの通常の送信APIは維持する。
Deviceを先に作り、Functionを構成してからdevice.begin()する。

```cpp
EspUsbDevice device;
EspUsbDeviceHidVendor pipe(device, 257);

void setup() {
  pipe.onOutputReport([](const EspUsbDeviceHidReport &report) {
    // report.data is borrowed for this callback only; copy what is needed.
  });
  if (!device.begin()) handleStartError(device.lastError());
}
```

reportSizeに応じたReport Countをdescriptorへ出力し、それを解析して必要容量を決める。
上記ならIN/OUT/Featureはreport ID込みで258 byte。FSでは一つのreportが複数packetに分かれる。
packetの大きさとreport全体の大きさを同じビルド定数へ結び付けない。
複合HIDでは既存どおり一つのinterface/report descriptorへまとめ、各Functionは共通の転送領域を参照する。

`CFG_TUD_HID_EP_BUFSIZE`、旧`CFG_TUD_HID_BUFSIZE`、`CFG_TUD_HID`指定（0を含む）は削除する。
古い指定を残したbuildは移行を促すエラーとなる。
`HidVendor::maxReportSize()`は現在65534で、内部転送長uint16_tからreport IDを除いた上限を返す。
SoCや速度のpacket上限ではなく、確保成功を保証する値でもない。使用可能メモリとHost側の対応は別の制約になる。
HSのpacket希望値は最大1024へ収めるが、P4/S31の実機検証はまだ行っていない。

HidVendorのsendInputは長すぎるpayloadを切り詰めずfalseを返すよう変更した。
短いpayloadは従来どおり送信できる。受け手が固定長を要求する場合は、宣言したpayload長までアプリ側で用意する。
sendの成功はコピーを保持して転送を開始したことを示す。Hostでの受信完了を待つAPIではない。
busy時のtimeoutとcallback内でtimeout=0を使う方針も維持する。workerからの待ち付き送信は拒否する。

## 独自HIDと容量

独自HIDは`EspUsbHidFunction`を基底にする。これは`EspUsbDeviceClass`を継承し、
共通のbegin/end、転送、確保エラー、停止を実装する。独自beginでは検証後に基底beginを呼び、結果を返す。
既存の`EspUsbDeviceHidCustom`を使うアプリ側に基底の変更は不要。
EspUsbDeviceClassから直接独自HIDを作っていた拡張は、新しい基底へ移行する必要がある。
独自beginで一部開始後に失敗した場合も、Deviceがendを呼んで基底の参照を戻す。
通常はDeviceがFunctionのbeginを呼ぶ。アプリからHID.beginを重ねて呼ぶ必要はなく、
未登録/開始済みFunctionのbeginはInvalidStateになる。稼働中のFunction.endはDevice全体を停止する。
独自派生クラスがcallbackや応答データを持つ場合、派生destructorの先頭でendを呼び、
workerが止まってからメンバーを破棄する。標準の各HIDクラスはこの終了順序を持つ。

`inputReportCapacity()` / `outputReportCapacity()` / `featureReportCapacity()`は、
その構成の共有interfaceに必要な最大byte数を返す。report IDを宣言したreportはIDの1 byteも含む。
開始前/終了後は0。構成変更は停止中に行い、次のbeginで再解析・再確保する。
HidCustomへ渡すreport descriptorは、稼働中も有効で変更されない配列として保持する。
既存のinputReportSize引数はpacketの選択に使われるが、転送領域の上限はdescriptorから求める。

解析はReport Size/Count/IDとglobal Push/Popを扱い、Main itemのbit数を同じID/typeごとに合算する。
使うIDだけの表を確保する。Push/Pop用の一時stackは実際の深さだけ作り、解析終了時に解放する。
切れたitem、stack不整合、Report ID 0の明示指定、番号付き/番号なしの混在、転送長超過等は開始時に拒否する。
これはHID usageや全semantic条件の検証器ではない。

| 領域 | 確保する条件 |
| --- | --- |
| 共有state / report表 | HIDを開始する場合に一度。使用ID数分のmetadata |
| IN DMA | Input reportがある場合だけ。最大Input report長 |
| OUT DMA packet | Output reportとinterrupt OUT endpointがある場合だけ。選択する速度の最大packet |
| OUT report組立 | 同上。最大Output report長 |
| 状態保存 | 定義したID/typeのreport長の合計。GET_REPORTとidle再送で使う |
| control staging | 定義したInput/Output/Featureの最大長。応答中の更新による混在を防ぐ |
| relative mask | relative Input項目のあるreportだけ。状態保存と同じ一括領域に含める |

INだけのHIDにはOUT DMA/組立とFeature保存を確保しない。GET_REPORT用のInput保存とcontrol応答領域は必要になる。
保存領域は全ID/typeとrelative maskを一括確保し、転送経路ではheapを使わない。
HIDを使わなければstate/driver/buffer自体をリンクしない。
確保失敗はbeginのNoMemoryで返し、共有HIDの参照・driver登録・全領域を回収する。
endはUSB workerとDMAを停止してから解放する。

## controlと受信

HID/report descriptor照会、Input/Output/Feature GET_REPORT、Output/Feature SET_REPORT、report ID別GET/SET_IDLE、
boot interfaceのGET/SET_PROTOCOLを独自driverで処理する。bus resetではreport protocolへ戻す。
SET_REPORTは方向/recipient/interface、ID/type、長さ、データ実受信量を検証し、
ID付き形式のprefix不一致はstatusを成功にする前に拒否する。
control経由の番号付きreportは、ID付き全長形式と、wValueだけでIDを指定するpayload形式を受け付ける。

interrupt OUTは一packetずつ受け、IDごとの宣言長またはshort packetでreportを組み立てる。
これにより、最大reportより短い別IDのreportがちょうどMPS倍数でも、余分なZLPを要求しない。
callbackへはIDを分離したpayloadと長さを渡す。受信領域の参照はcallbackの間だけ有効。
callbackからend・Function破棄・構成変更を行わない。

GET_REPORTはID/typeごとに保存した状態を返す。初期値はIDとzero payload、Inputは送信受理時、
Output/FeatureはSET_REPORTの完了時に更新する。短いpayloadは保存長も短くなり、黙ってpaddingしない。
制御応答開始時に専用領域へcopyするため、途中でアプリが状態を変更しても一つの応答内で混ざらない。
未知のID/typeや方向・interfaceの不一致はSTALLする。

Inputのrelative項目は送信成功後にzeroへ戻し、absolute項目は保持する。
MouseのX/Y/ホイールをidle周期で繰り返し適用しないための処理で、descriptorのbit位置からmaskを作る。
boot mouseは3 byte、通常のMouseは4 byte。Keyboardもboot 8 byte/NKRO 29 byteへ切り替える。
protocol変更時のInput保存はneutralへ戻す。独自boot拡張は番号なしreportと必要な長さを持つものに限定し、
不整合な短いboot descriptorは開始時に拒否する。

SET_PROTOCOLで形式が変わる場合は、未完了の旧INをhardwareで停止してからstatusを返す。
旧完了eventをcoreのFIFO fenceで排出するまで、明示送信とidle再送の両方を保留する。
固定時間のdelayやendpointのSTALL/clear-haltでDATA PIDを初期化する方法は使わない。
fence予約がqueue満杯なら、既存のidle用worker jobで再試行し、bufferを早期再利用しない。
protocol変更時に追加heapや専用taskを確保しない。

boot-capableな番号なしInputは、現在の形式の正確な長さを要求する。
例えばNKRO有効時のreport protocolへ8 byteのboot配列を直接送る呼出しはInvalidSizeになる。
通常のKeyboard helperは形式を自動選択する。形式選択と送信の間にHostが切り替えた場合は、
不一致のpacketを送らず拒否するため、アプリ側で再試行できる。Mouseの4→3 byte変換は維持する。


## Feature値の更新

```cpp
uint8_t value[] = {1, 2, 3};
esp_err_t error = pipe.setFeatureReport(value, sizeof(value));
```

begin成功後に使う。payloadにreport IDを含めず、復帰後は元配列を再利用できる。
省略ID=0は複合ならそのFunctionの割当ID、単体なら番号なしまたは一意なFeature IDを選ぶ。
複数のFeature IDがあるcustom HIDはIDを明示する。複合構成で別FunctionのIDを指定することは拒否する。
未開始/停止処理中はInvalidState、FeatureなしはNotSupported、長さ超過はInvalidSize。
Hostへのinterrupt送信やonFeatureReport通知は発生せず、次のGET_REPORTに反映する。
アプリtaskとUSB workerから呼べ、読出しとの間で一つの値を整合して保持する。

bus reset/unconfigureではInput/Outputをneutralへ戻し、idleを無効化する。
FeatureはDeviceの現在の設定として保持し、endまたは次のbeginで初期化する。
そのため、Hostが接続する前にsetFeatureReportした値も、列挙時のresetで失われない。

## idleと設定変更

HostのSET_IDLEの値×4 msで、対象Input reportの状態を自動再送する。0は自動再送なし、ID 0は全Input IDへの指定。
GET_IDLEはID別の設定値を返す。既定は0を維持し、明示的なsendInput/sendReportは同じ値でも送信する。
idle設定は自動再送を制御し、アプリからの送信を暗黙に捨てるfilterにはしない。
期限は前回の転送完了を基準にし、INが使用中なら完了後に送る。複数IDは期限の古い方を選ぶ。
期限直前の周期変更は次のreport後に反映する。既に投稿したINは、idleを0にしても完了まで保持する。

再送jobはHostが有効にした間だけworkerへ再投稿し、タイマー/メッセージ用のheapを追加しない。
reset/endで停止する。suspend中は新規Inputを拒否し、idle再送はresume後に再開する。
S3 FSのVendor HID 2 IDで実bus休止・再開を確認した。試験範囲は後述。

KeyboardのonOutputReport/onProtocol/enableNkroとHidVendorのonOutputReport/onFeatureReportは、
CDCと同じesp_err_t返却・停止中設定に揃えた。開始中はInvalidStateとして既存設定を保つ。
戻り値を無視する既存呼出しの形は維持できる。void戻り値のmember function pointerを使う拡張は型の変更が必要。
protocol()はatomicな値のsnapshotを返し、workerからの更新と競合しない。

## 実験と移行判断

利点はビルド設定なしの大きなreport、不要なOUT/Feature資源の省略、未使用HID class全体のリンク除外、
構成ごとの確保量と失敗結果の明示化。不利益は開始時のdescriptor解析/確保、
独自HID基底の移行、および不正descriptor・長すぎるsendの明示的拒否である。
通常のKeyboard/Mouse送信を変更する必要はない。

S3–S3の初期3試験は139.25秒で成功。新規`hid_transport`のfocused試験も1 passed、95.25秒。
payload 1/63/64/65/127/128/129/255/256/257/511/1023 byteのIN、interrupt OUT、
control Output（prefixあり/なし）・Feature、無効要求後の復帰、idle値、boot/NKRO切替とLEDを確認した。
descriptor-onlyでは最大65535 byteの領域と反復heap回復を確認した。最大長そのもののwire転送は行っていない。
Host probeは複数packetを明示的に連結して照合しており、任意のOSアプリのread APIの挙動を証明するものではない。

標準の`--combo all`は`runtime-hid-all.xml`、9 passed、0 skipped、0 unfinished、474.47秒。
この時点では176取得点の確保失敗/回復を含む。全caseで両board STOPPED、panicなし。
その後の終了順序の補強と解析stackの故障注入は、追加のfocused試験で確認する。

終了順序とstack故障注入を加えた`hid-transport-final.xml`は2 passed、91.43秒。
稼働中Vendor HID/Keyboard破棄、callbackを有効に保った停止、194取得点の失敗/回復を確認した。
開始成功時のstartError=ESP_OKも、続く`hid-transport-errors.xml`で1 passed、53.04秒を確認した。
未登録HIDのbegin拒否と既存7 HIDの通信継続も、`hid-registration-final.xml`の1 passed、44.01秒で確認した。

GET_REPORT/idle追加後の`hid-report-state.xml`は2 passed、127.44秒。
2 IDのInput/Output/Feature、初期値、Featureのreset越し保持、並行更新中の200読出しの整合性を確認した。
20/100 ms指定は、このS3 FS接続で平均20.873/108.002 msだった。大きいreportの複数packet転送時間も含む観測値で、
全環境での精度保証や性能benchmarkではない。ID単位/全体の再送停止を確認した。
relative maskとMouseのboot/report形式を加えた`hid-state-relative.xml`は3 passed、123.33秒。
この時点で200取得点の失敗/回復、Feature更新の追加確保なし、相対値をzeroへ戻したidle再送も確認した。
最大長のdescriptor-only確認は、追加保存領域を考慮しINのみの65535 byte構成で行う。

GET_REPORTの状態保存と独立した応答用copyに、定義したreportの長さ分とcontrol staging分のRAMが追加で必要になる。
全OS、P4/S2/S31、休止中のprotocol変更・切断等の全組合せ、期限近傍の詳細精度、
未完INを伴うprotocol変更の競合は追加検証が必要。

最終の標準`--combo all`実行`hid-report-idle-all.xml`は**10 passed、0 skipped、0 unfinished、520.13秒**。
200取得点の失敗/回復とFeature setterの追加確保なし、短いboot定義の拒否、設定の停止中契約も含む。
全caseで両board STOPPED、panicなし。3最小build/nmはHID未使用時のdriver/状態保存/idle処理のリンク除外を維持した。

規格参照: [USB HID 1.11](https://www.usb.org/sites/default/files/documents/hid1_11.pdf)。

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

## 標準interface/endpoint要求による回復

SET_INTERFACE alt 0では、そのHID interfaceのIN/OUT DMAを停止し、旧完了eventのfence後に
送信中フラグと受信途中の組立て状態を解放し、両endpointをDATA0へ戻す。
SET_FEATURE ENDPOINT_HALTでは指定方向を停止し、CLEAR_FEATUREではその方向をDATA0へ戻して再開する。
もう一方のendpointの転送・DATA PIDは維持する。OUTは内部で再投稿する。

HID sendInput/sendReportは内部bufferへのcopyの受理を返すため、受理済みでも未完了のレポートは
これらの初期化で破棄され得る。HALT中とfence待ちは新しいINを受理しない。
受信途中のOUTを新しいレポートへ連結せず、未完成のレポートのcallbackも発生させない。
GET_REPORT用の保存値、protocol、idle設定は保持する。idle有効時は保存値の通常の再送を再開する。
これらの標準要求はbus resetとは異なり、protocolをreportへ戻す操作ではない。
SET_PROTOCOLによるformat変更では従来どおりINだけを止め、PIDを維持する。

新たな専用task・転送時heap確保を追加せず、既存idle jobを方向別fence予約にも使う。
追加費用はHIDを使う場合の方向別drain/PID-reset状態と回復コード。
公開send/callback APIの形は変えない。確実な配送確認が必要な用途ではHostとアプリで確認・再送を行う。


## HID標準要求の回復

2026-10-27。HIDのSET_INTERFACE alt 0とHALT/CLEAR_FEATUREに方向別DMA停止・fence回復を追加した。
新規hid_standardは129 byteレポートの部分IN/OUTを3回再初期化し、未完成OUTの通知なしと再通信を確認する。
HALT有無のCLEAR_FEATUREを両方向で行い、反対方向のDATA1から残り65 byteを継続する。
両方向とも部分転送後にHALT/解除した条件ではHost pipeだけを再生成し、余分なSET_INTERFACEで回復を隠さない。
公開HID APIはcopy受理のまま。Hostの初期化で受理済みの未完レポートが破棄され得ることを明記した。

`hid-standard-related.xml`はhid_standard、hid_protocol、hid_transport、hid_report_stateの
**4 passed、0 skipped、0 unfinished、636.88秒**。8 serial log全てSTOPPED、panicなし。
原本`/tmp/pytest-embedded/2026-10-27_07-45-09-725494/`、コピー/JUnit/transcriptは
`validation/.pytest-results/hid-standard-related*`。
Vendor copy最小buildのnmではHID本体・方向別drain処理のsymbol不在を確認した
（`hid-standard-vendor-copy-build.txt`、`hid-standard-vendor-copy-nm.txt`）。

plugin repoは1.10.0、commit `698bb29`で、combo/profileの最新ローカルガイドを再確認した。
新試験もconftestや独自hookなし。標準combo allで29件を収集したが、現在の全29件一括実行は未完了。
SET_INTERFACEとidleの同時条件、人工的な旧完了/queue満杯/reset/endの全競合順序、他SoC/OSは未検証。
残るDWC2/ABI/header依存と他クラスの回復検証も継続する。


## 実bus休止中のidleと再開

`hid_report_state`を拡張し、HostのLLから実際のroot portを3回休止・再開した。
Inputのwire長8/130 byteの2 IDをそれぞれidle値5/25（20/100 ms）に設定し、
休止中のHost受信数が150 ms間変わらないことと、Deviceの明示Input拒否を確認した。
各resume後は350 msに短いreport18件・大きいreport4件を受信し、GET_IDLEとGET_REPORTの値も維持。
再送停止後の静止、200回のFeature読出し整合性、最後のbus resetでの状態も通過した。

製品`512edb6`のまま**1 passed、0 skipped/error/failure/unfinished、80.33秒**。
2 serial全てSTOPPED、STOP_FAILED/panicなし。原本
`/tmp/pytest-embedded/2026-10-28_16-07-53-418156/`、結果は`.pytest-results/hid-suspend*`。
S3 FSのHost起点休止の結果で、Host側の未完periodic Inputがある構成を含む。
未完reportの全packet位置やboot mouse/keyboardの全状態、remote wakeup、OS省電力の検証ではない。
製品コードの追加変更や新しいtask/bufferはない。
