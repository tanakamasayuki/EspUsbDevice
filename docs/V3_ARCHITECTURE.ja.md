# v3 実装の出発点と判断契約

2026-10-26。対象は[開発計画](V3_DEVELOPMENT_PLAN.ja.md)の段階1〜3。
実装の優先順と既存利用への影響判断は[V3_API_DECISIONS.ja.md](V3_API_DECISIONS.ja.md)を優先する。
以下のregistryや結果型を先にすべて完成させず、Vendorの自然な利用経路に必要な分だけ作る。
これは実装済みAPIの説明ではない。以下のAPI名・型名は実装を開始するための案であり、
実験で改善できる。互換aliasを設けず、採用後はexample・試験・この文書を同じ変更で揃える。
CH32RVへコードを移植する作業、OEP固有protocolの実装は含めない。

## 判断を分ける

先に守るものは、未使用クラスを参照しない依存方向、資源の所有、拒否と受理の区別、
転送終了時の一度だけの返却、切断・停止時の回収、設定失敗の可視化である。
実測で選ぶものは、既定容量、queue深度、worker/polling方式、通知をまとめる頻度、
FIFO配分、DMA bufferの調達方法、直接転送の上限・分割方法である。
前者の実現方法にも問題があれば実験で修正する。ただし黙って通知を落とすなど、
所有権を曖昧にして高速化したことにはしない。

## 依存方向と置換境界

```text
利用者 → Device / Function (HID, CDC, Vendor, …)
                       ↓
            descriptor / endpoint / transfer契約
                       ↓
                EP0・bus state・DCD
                       ↓
               ESP PHY / DWC2 / RTOS

任意helper (SD, OTA, IP, keyboard layout等) → 必要なFunction
```

- Deviceは登録されたFunctionだけを見る。全クラス列挙switch、全クラスへのextern参照、
  全クラス分の最大配列、静的な自動起動は持たない。
- Deviceは登録Functionより長く生存させる。DeviceとFunctionはnoncopyable/nonmovable。
  初期案はFunctionのコンストラクタでDeviceの侵入型listへ登録する。登録はheapを使わない。
  `begin()`中以降の登録解除・追加は拒否する。コンストラクタで登録できなければ
  Functionの登録errorとして照会でき、黙って有効なFunctionとして扱わない。
  Function破棄には先に成功した`end()`が必要。Stoppedで破棄するとlistから登録解除する。
  宣言順はDevice→Function、終了順は明示end→Function破棄→Device破棄となる。
- 各クラスとhelperを別translation unitへ分ける。共通headerからSD、IP、OTAの実装headerをincludeしない。
  umbrella headerを残しても宣言の入口に留める。vtable/関数表はいずれでもよいが、リンク実験E1で決める。
- `begin()`で登録listからinterface/endpoint要求を収集し、能力表と照合して全体を計画する。
  descriptorの長さを数えるpassと書き出すpassを分け、同じ構成から両方を生成する。
  最後まで成功してからattachする。失敗時はPHY・taskを含め取得逆順で戻す。
- 暫定TinyUSB adapterは内部だけに置く。クラスdispatch表もadapter内とし、
  新Functionと旧driverが同じinterfaceを処理しない。旧driverを無効化した結果が
  Coreの`libarduino_tinyusb.a`へ解決されないことをmapで確認する。
- 旧DWC2は`usbd_spin_lock`、event、EP0 status処理に結合しており独立部品ではない。
  まずtransfer境界に閉じ込め、EP0/DCD一括変更をクラスAPI変更から分ける。
  分離のために多数のTinyUSB内部型が上へ漏れるなら、段階2で最小EP0/DCDを前倒しする。
  恒久的なbackend選択APIは作らない。

暫定adapterの実装入口は既存`internal/EspUsbDeviceAppDriver.*`の
`usbd_app_driver_get_cb()`である。現状はCCIDが一つのtable pointerを登録するだけで、
複数Functionを順に登録すると前のtableが置き換わる。ここをそのまま拡張点として公開しない。
begin時に選択済みFunctionだけのdispatchを組み立て、旧built-inを置換したクラスは内部設定で外す。
新クラスのcontrol/endpoint completionを共通契約へ変換する。旧`EspUsbDevice.cpp`の
全クラスcallback参照とruntimeのNCM専用drainも分離しないと未使用クラスを落とせない。
旧`usbd_edpt_xfer`は長さuint16_tであり、段階2のadapter限界と最終APIの限界を区別する。
このadapterでcancel/reset時の安全な返却が証明できなければ、E2で不成立と記録し、
最小DCD/EP0の置換を前倒しする。返却保証だけを弱めて段階2を合格にしない。

## lifecycle と設定

| 操作・状態 | 初期契約 |
| --- | --- |
| Stopped | 宣言・構成・buffer指定・callback設定を変更できる。descriptor/転送資源は未確保 |
| `begin(config)` | validate → allocate → endpoint/descriptor準備 → runtime開始 → attach。成功は列挙成功を意味しない |
| Started / Attached | 構成は固定。`configured`、`suspended`、実効speedは別々に照会する |
| `detach()` | ホストから離脱し受付を閉じ、受理済み転送を終了させる。構成資源は保持。再attach可能 |
| bus reset / unconfigure | ホスト設定と転送世代を更新し、旧転送をReset/Unconfiguredで終了。Function構成は保持 |
| `end()` | 新規受付を閉じ、detach、DMA停止・取消し、完了回収、worker停止、資源解放。成功復帰後にcallbackやDMAがbufferへ触れない |
| begin失敗 | Stoppedへrollbackし、失敗理由と対象Function/資源を返す。次のbeginを許す |
| end失敗 | 停止を証明できないbufferを解放しない。Stopping/Fault状態と理由を残す。単にtimeoutでStoppedへ進めない |

最初は容量・クラス数・descriptor・transfer modeの変更をStoppedだけで認める。
実行時設定とは「再ビルドなしにend→設定→beginできる」ことをまず実現する。
接続したままdescriptorを書き換えることとは区別する。Audioのalternate/rate、MSCのmedia状態、
CDC line coding等のprotocol上の動的状態は稼働中に扱う。
稼働中のring拡張は必要性が実測で出るまで実装しない。

実体の寿命とコピー方針:

- 数値設定は値として保持。文字列・descriptorはbegin時に必要量をコピーし、成功後は元の設定変数を解放できる案から始める。
  begin前に設定APIで借用する場合は、その時点からbeginの復帰まで元データを保持する契約を明記する。
- 大容量のユーザーbufferとcallback contextは借用。借用期間をAPI単位で明記する。
  class用ringは成功したendまで、個別転送bufferはその転送の終了通知まで保持する。
- 自動確保はbegin時。転送経路でのheap確保はしない。任意helperは有効化時だけ確保する。
  ユーザーbuffer指定時は容量・alignment・メモリ種別を検証し、知らないうちに自動bufferへ置換しない。

## 共通transfer契約

まずVendorで以下を確かめ、CDC/MIDIのbyte stream、HIDのreport、Audioのframeへ意味を写す。
すべてを同一の`write()`へ押し込まない。

| 経路 | 戻り値・寿命 |
| --- | --- |
| `write(data, n)` | コピーして受け入れたbyte数。partialは許す。復帰後は元buffer再利用可。wire完了ではない |
| `trySubmit(buffer, options)` | 非同期の借用転送。受理はticket、拒否はBusy/InvalidBuffer等のerror。拒否時は借用せず通知もしない |
| transfer completion | ticket、buffer identity、requested/actual bytes、Success/Cancelled/Reset/Disconnected/IoErrorを渡す。一度だけ。実際にpeerアプリが読んだことまでは保証しない |
| `cancel(ticket)` | 取消し要求の受理とbuffer返却は別。完了と競合しても終了結果は一つ。DMAが止まる前には返さない |
| FIFO RX | `available/read`。満杯時のbulkは受信再armを止めてbackpressureをかけ、黙って上書きしない |
| borrowed RX | 利用者がbufferを投稿し、完了通知で返却。次の受信には再投稿が必要。部分受信・ZLP・切断も同じ返却経路 |
| callback RX convenience | pointerの有効期間はcallback内だけ。保持したい場合はborrowed RXを使う。意味を同じ名前で混在させない |
| timeout | 待ちを打ち切っただけではbuffer返却にならない。sync wrapperはcancelと返却確認まで行うか、未完ticketを返す。生pointerの寿命を隠すwrapperは禁止 |

`trySubmit`は当初strictなcopyなし契約にする。不適合bufferを黙ってbounceしない。
`write()`はコピー経路。将来copy許可の転送が必要なら別option/名前で実効方式を識別する。
DMA allocatorを用意し、alignmentとcache lineの専有容量まで含むBufferViewにする案をE4で検証する。
TX/RXのcache操作はdriverの責務。利用者へSoCごとのcache flushを要求しない。
P4で隣接変数とcache lineを共有するRX bufferを許可するかは、E4の検証まで許可しない。

`capacity`（保持可能量）、`transferBytes`（一回に投稿する量。内部armは分割可）、`maxPacketSize`（wire packet）、
`queueDepth`（受理できる未完transfer数）を区別する。旧TinyUSBの65535/32768をv3の公開上限に引き継がない。
ハード制限内の分割を実装するか、上限を返して拒否するかはE3/E4で選ぶ。無言の切り詰めはしない。

bulk INの終端はtransferごとに明示する。初期案は`Continue`と`EndWithShortPacket`。
後者はMPS倍数長なら必要なZLPまで含めて一つのticketとし、その完了前に次のticketが割り込まない。
非倍数長は最後のshort packetで終了する。これはホスト側read要求長を知る機能ではない。
bulkはbyte streamであり、`write()`一回とホストread一回の境界一致は保証しない。
通常writeのflush/終端規則と長さ0の扱いはE3で決める。Audio/Video isoへbulkのZLP規則を適用しない。

## context、通知、競合

初期実装はUSB workerでclass処理と直接完了通知を行い、ISRは最小event生成に限る。
USB callbackはISRではないが、block、重い計算、`end()`、構成変更を禁止する。
同じendpointへの次の`trySubmit`と、他taskへのsignalは許す。ユーザーcallbackを内部lock保持中に呼ばない。
submitのcallbackはinlineには呼ばないが、別coreのworkerが呼出元の復帰前に完了を処理する可能性がある。
利用者はsubmit前にcontextを準備する。返されたticketの代入完了だけにcallbackを依存させない。

`end()`はアプリtaskから呼び、同時submitを閉じて進行中callbackの終了を待つ。
同一Deviceの設定・lifecycleは利用者が直列化する。転送submit/cancelは複数taskから安全に使える設計を検証する。
stream write同士のbyte混在を禁止したい用途は上位で直列化する。ライブラリはstreamのメッセージ境界を発明しない。

遅延通知/pollingはE2の比較対象。必要なら`pollEvents()`等の明示入口と、停止時のdrainを定義する。
completionをqueue overflowで捨てない。受理時に終了記録の場所を確保し、無ければBusyを返す。
状態変化通知をまとめてもtransfer返却通知を合算しない。bus reset後の古いeventを新しいendpointへ渡さないよう世代を持つ。
既定queue深度、worker優先度・core配置、統計の有無は実測後に定める。

## 公開APIの対応案

| 論点 | 現ESP / CH32RVでの差 | v3出発案とCH32への調整候補 |
| --- | --- | --- |
| Device + Function | ESP Class、CH32 Function。双方コンストラクタで関連付け | 名前より意味を揃える。登録・begin・endを共通化、ESP runtime optionは別構造 |
| buffer設定 | ESPはCFG、CH32 CDCはsetBuffersがある | クラス別configで容量/借用buffer、設定拒否をerrorで返す。稼働中の無言無視をしない |
| direct TX | ESPはbuild切替かつalignment制限、CH32はbounceも可能 | strict submitとcopy writeを区別。CH32も同じ契約へ合わせる案を提示 |
| 完了 | ESPはUSB task、CH32はdeliverEventsでbyte数を合算 | ticket単位の終了と、集約可能な通知を分ける。contextを明記 |
| RX | ESPはcallback中pointer、CH32は外部buffer/保留あり | FIFO、callback中借用、投稿buffer返却を明確に分ける |
| エラー | esp_err_t/bool/lastError等が混在 | begin/configure/submitは結果を直接返す。Print互換writeはbyte数、診断照会は補助 |
| 能力照会 | ESP static容量getterが多い | 要求値・実効値・controller capabilityを別のinstance APIにする |
| descriptor拡張 | 内部TinyUSB型へ依存する入口あり | class非依存のSetupPacket/ControlReply/descriptor writerへ閉じる |

エラーはInvalidArgument、InvalidState、Unsupported、NoMemory、ResourceExhausted、Busy、
Timeout、IoErrorを少なくとも識別する。ESPの追加情報を保持しても共通の成功/失敗判定に必須にしない。
`lastError`だけを転送結果の根拠にしない。並行転送で上書きされるためである。

### 利用コードの形（未実装、型名は仮）

```cpp
EspUsbDevice usb;
EspUsbDeviceHidKeyboard keyboard(usb);

void setup() {
  EspUsbDeviceConfig config;
  config.product = "Keyboard";
  auto result = usb.begin(config);  // heavy resources start here
  if (!result) reportError(result);
}
void loop() {
  // Basic keyboard actions remain small; application decides when to send.
  if (usb.configured() && buttonPressed()) keyboard.press('a');
}
```

複合構成でもDeviceは一つ。CDC設定は別Functionや別個体へ波及させない。

```cpp
EspUsbDevice usb;
EspUsbDeviceHidKeyboard keyboard(usb);
EspUsbDeviceCdcSerial serial(usb);
// setup: check every Result before begin
serial.configure(CdcConfig{.rxCapacity = 512, .txCapacity = 1024});
usb.begin(deviceConfig);
// after configured: Print operations copy their input
serial.println("ready");
// reconfigure: require successful end before reusing borrowed storage
usb.end();
serial.configure(CdcConfig{.rxCapacity = 2048, .txCapacity = 512});
usb.begin(deviceConfig);
```

streamingでは一度受理されたbufferを完了前に書き換えない。callback中は次の準備済みbufferの投稿まで。

```cpp
// setup, illustrative: allocate DMA buffers explicitly, then configure depth
vendor.configure(VendorConfig{.txMode = TxMode::Borrowed, .queueDepth = 2});
vendor.onTransferComplete([](void *context, const Completion &done) {
  returnToProducer(done.buffer, done.status, done.actualBytes);
}, producerContext);
// producer task: buffer and context have been prepared before this call
auto result = vendor.trySubmit(buffer.view(bytes),
    TransferOptions{.termination = Termination::Continue});
if (!result) recycleUnsubmitted(buffer);  // rejected: still ours
// accepted: completion owns the return path, including disconnect/reset/cancel
```

これらは構文の提案であり、現headerに通ると主張しない。対象APIを実装する変更で最小headerと
新しいcompile-onlyの利用例を作り、戻り値を無視しない実例へ置換する。API名はその時点で一本化する。

## 実装へ渡す能力表の注意

SoC能力はportごとの表にし、runtimeのP4限定分岐をS31へ拡張し続けない。
現行DWC2 tableではS2/S3/P4 FSはendpoint数7、IN数5（EP0を含む）、FIFO256 words、
P4 HSは16/8/1024。現validatorのendpoint番号上限と一致しない箇所はE6で確認する。
数字だけで追加endpointを許可しない。EP0、RX FIFO、DMA endpoint-info、各IN FIFOを同じ計画で計上する。
HS controllerはFSへfallbackし得るので、FS/HS descriptor双方と構成可否を用意する。
S31はHS port番号がP4と同じとは限らず、Core、PHY、DCD、board配線を新規に検証する。

EP0では新SETUPによる前要求の取消し、data/status方向、SET_ADDRESSの適用時点、
STALL後の復旧、GET_DESCRIPTORの短い読出し、SET_CONFIGURATION/SET_INTERFACE、
reset/suspend/remote wakeupを独立に試験する。class control handlerへDWC2 registerを公開しない。
遅延control応答を許す場合は要求tokenと世代で識別し、新SETUP後の応答を拒否する。

実験一覧と採用基準は[V3_EXPERIMENTS.ja.md](V3_EXPERIMENTS.ja.md)、
実行結果と次の作業単位は[V3_HANDOFF.ja.md](V3_HANDOFF.ja.md)を参照する。


## 専用TX FIFOとendpoint番号

DWC2のIN FIFO番号は、選択済みIN endpoint maskでEP0からの順位を使う。
USB endpoint番号に空きがあってもFIFOは0から詰め、EP0はFIFO 0へ固定する。
割当て・endpoint activate・個別flushは同じ対応を使う。maskは開始時に決まり、転送中に組み替えない。
追加のmap配列・transfer record field・heap取得はない。
Audio＋CDCでIN endpoint 1/2/4/5を使う構成を実測し、従来のendpoint番号=FIFO番号ではCDC INが進まない問題を修正した。
詳細・検証範囲は[Audio＋CDC](V3_AUDIO_API.ja.md#audio＋cdcの同時通信とtx-fifo割当て)を参照。
