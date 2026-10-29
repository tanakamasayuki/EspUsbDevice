# v3 Remote wakeup

2026-10-29。S3–S3で基本経路を確認した。能力広告、Host許可、アプリ要求を別に扱う。

```cpp
EspUsbDeviceConfig config;
config.remoteWakeupSupported = true; // 既定false。begin時のdescriptorへ反映。
device.begin(config);
// loop()等のtask contextで、外部イベントがあったとき:
if (device.suspended() && device.remoteWakeupEnabled()) {
  esp_err_t result = device.remoteWakeup();
  // ESP_OKはqueue受付。実際の復帰はsuspended()で観測する。
}
```

remoteWakeupEnabled()は現在の構成に対するHostの許可を返す。reset/endでfalseとなる。
要求はesp_err_tを直接確認する。停止中、DescriptorsOnly、未構成、非suspend、未許可、ISR、
既にpendingの場合はInvalidState。queueに空きがなければTimeout。
workerからの非同期要求も可能。受付後のresume/CLEAR/reset/再構成/停止で不要になった要求は実行しない。
照会もtask context用で、ISRではfalseを返す。runtimeの停止開始後も照会はfalse、要求はInvalidStateとなる。Hostの許可をアプリが自分で設定するAPIは設けない。

常駐timer/taskや独立したpending領域を増やさず、既存worker/queue、許可byteと構成世代を使う。
実行時に5 ms以上の保守的な待機後、状態を再確認して信号を出す。
信号区間はROM delayの2000 usをcontroller lock内で使う。使用時に実行coreの割込みが約2 ms遅れるため、
他の時間制約との調整はアプリの判断になる。未使用時にこの待機を行わない。
これはbus復帰の要求であり、SoC sleepやPCの電源管理・電気適合性まで検証したものではない。

基本試験は1 passed、50.20秒、2 logs STOPPED/panicなし。
能力なしのSET_FEATURE拒否、11不正状態要求、6回の無信号、3 Device起因wake、10双方向payload、
CLEAR後の拒否とHost再初期化によるbus reset後の許可消去を確認した。
Hostのwake statusとSOF frame進行、Deviceの休止解除と実通信を合わせて確認する。
初期Hostはclock gateを含まない休止手順でframeが再開せず失敗した。固定SDKは変更せず、
試験用Hostにclock gate/ungateを含む休止・復帰とIRQ policy保存/復元を実装して成功した。
記録はvalidation/.pytest-results/remote-wakeup-{first,diagnosed,armed,auto,clock}*。
追加境界試験は1 passed、45.24秒、2 logs停止/panicなし。workerを保留して要求をqueueしたまま
bus resetを入れ、再列挙後の許可消去と通常通信を確認した。停止前の重複要求も拒否した。
停止待機中に17回、停止完了後に32回、別taskから要求/照会して全て拒否/falseとなった。
修正前は停止待機中に照会がtrueのままで失敗し、既存g_stopを使って受付/実行を抑止した。
停止完了は進行中処理の終了を待つ。停止開始前に既に出力中の信号を即時中断する契約ではない。
記録はremote-wakeup-{reset,boundaries,stop-before,stop-after}*。
S2/P4は停止guard後80e8676でもcompile・機械種別・archive・29依存・native pulse/IO/delay出所・
未選択class除外を確認した。記録remote-final-cross-audit.json。全47件は実行中。S2/P4実機とPC OSは別に残る。

S3最小probeはinclude_onlyの追加Flash/globalが0のまま。Device宣言のみはFlash261957/global21840 byte。
旧archive基準261913/21824に対し+44/+16で、Device object自体は196 byteで変わらず、共通生存確認lockが新たにリンクされる。
未使用のwake要求・pulse/worker startup/PHY startupや他class stateは含まれない。停止guard後も同値で、記録remote-final-unused-audit.json。

## 実装前の調査記録

## 規格と状態

[USB 2.0原典の保存版](https://bitsavers.trailing-edge.com/components/usb/USB_2.0_2000.pdf)の
§7.1.7.7（本文156頁）、§9.2.5.2、§9.4.5（255頁）を確認した。
公式配布入口は[USB-IF](https://www.usb.org/document-library/usb-20-specification)。

- 能力のdescriptor広告と、HostのSET/CLEAR_FEATUREによる許可は別の状態。
- 許可の既定は無効で、bus resetで消える。GET_STATUSに現在の許可を返す。
- Deviceがresumeを要求できるのはbusが連続5 ms idleになった後。
- Deviceのresume信号は1～15 ms。Hostが引き継ぐresume処理とDeviceの信号時間を混同しない。

候補はconfigの明示的な能力設定（既定false）、Host許可の照会、要求APIの3点。
要求成功を「Hostが既に復帰済み」と扱わず、復帰は既存suspended状態で観測する。
停止中、DescriptorsOnly、未構成、能力なし、Host未許可、非suspend時は信号を出さない。
未使用時にtimer/task/bufferを常駐させない。ISRやUSB workerからの呼出し契約とend/reset競合も先に定める。

現DCDのespusb_controller_remote_wakeupはIO有効性やlifetimeの確認なしにDCTLを操作する。
公開前に上位の状態判定とcontrollerの停止・resetとの同期が必要。
platformのvTaskDelay(pdMS_TO_TICKS(1))は「1 ms以上の信号」の保証にならない。
現在のS3 SDKはCONFIG_FREERTOS_HZ=1000だが、tick途中の呼出しでは待機が1 ms未満になり得る。
時間方式は実験で選び、taskから測った経過時間を電気信号の認証結果にはしない。

## ピン留めしたHost環境

Core3.3.12のローカルesp_idf_version.hは5.5.5。usb_host.hにsuspend/resume公開APIはない。
[同tagのHCD](https://github.com/espressif/esp-idf/blob/v5.5.5/components/usb/hcd_dwc.c)の
_intr_hdlr_hprtにはREMOTE_WAKEUP caseがなく、未知eventのdefaultはabortする。
[HAL](https://github.com/espressif/esp-idf/blob/v5.5.5/components/hal/usb_dwc_hal.c)にはwake eventのdecodeがある。
従って、このHostで単にwake IRQを有効にして自動復帰を期待する手順は採らない。
これはsource上の確認であり、現ベンチでabortを発生させた結果ではない。
取得原本は/tmp/espusb-v3-remote-wakeup-research/に置いた。

[最新ESP-USB](https://docs.espressif.com/projects/esp-usb/en/latest/esp32s3/usb_host.html)は
Hostのglobal suspend/resumeとremote wakeupを説明しているが、これをCore3.3.12の能力と扱わない。
この確認のためだけにCoreやpluginを暗黙に更新しない。

初期実験は既存suspend_resumeと同様、probe内でroot portを制御する手動Hostを候補とする。
全transferの回収後にsuspendし、SDKの未知eventへ渡さずにwake statusを観測し、Hostのresumeを続ける。
IRQ maskの保存復元・pending statusの処理・通常通信への復帰も試験の一部とする。
Hostから時刻でresumeするだけの試験をDevice起因の成功と数えない。
無許可/CLEAR後には信号なし、許可後には検出あり、その後payload往復、reset後の許可消去を区別して照合する。
S3–S3でこの経路を実測してからAPIと時間方式を採用する。PC OSやS2/P4の実機試験は別に残る。

## 実装に使った候補と残る境界試験

能力設定、Host許可照会、非同期要求をDeviceへ置く。要求成功はqueue受付を示す。
追加の常駐timer/taskを作らず、既存workerのdeferred callを使う案を先に試す。
coreの既存Host許可byteを許可/pending flagへ整理できれば、独立したpending領域を増やさずに重複要求を拒否できる。
既存configurationEpochをcallbackへ渡し、reset/再構成後の古い要求を実行しない。
DeferredCallは通常のbus世代判定より先に実行され、overflow回復でもdrainされるため、
callback自身がepoch・stopping/recovering/resetting・構成・Host許可・suspendedを再確認する必要がある。
公開要求と照会はcontroller lock内でcoreの生存を確認し、停止側も同じlockで拒否状態とcoreの切離しを公開する。
queueの破棄前に公開側を締め、worker/PHYの終了順序を保つ。

idle時間は要求処理時に保守的に待ってから状態を再確認する案なら、未使用時の時刻領域も不要になる。
信号時間はROMの短いdelayとcontroller lockによる上限管理を候補とし、2 ms程度のIRQ遅延の影響も含めて実測する。
この方式を採用するかは実機のK検出/Host復帰/通常通信と、拒否・reset/end境界を確認して決める。
許可なし・未構成・非suspendでは信号を出さず、受付後の状態変化で要求を取消す契約を明記する。
