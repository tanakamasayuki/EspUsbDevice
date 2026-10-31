# v3 設計の出発点と所有権の契約

2026-10-26に、v3の実装を始めるために決めた設計の契約。対象は[開発計画](V3_DEVELOPMENT_PLAN.ja.md)の段階1〜3。

この文書には二種類の内容がある。

- **今も守っている契約**：依存の向き、資源の所有、一度だけの返却、停止時の回収など。
- **当時の名前・構文の案**：`trySubmit`、`ticket`、`detach()`など。実装では別の名前や形になった。
  対応は[現在の実装との対応](#現在の実装との対応)にまとめた。

APIの優先順と既存利用への影響は[API判断](V3_API_DECISIONS.ja.md)を優先する。
CH32RVへのコード移植と、OEP固有protocolの実装はこの文書の対象外。

## 現在の実装との対応

2026-10-31時点。左が当時の案、右が実装。

| 当時の案 | 実装（`src/EspUsbDevice.h`） |
| --- | --- |
| `trySubmit(buffer, options)` | `writeDirect(buffer, length, termination)`、`readDirect(buffer, capacity)` |
| ticket | `EspUsbTransferSubmission::id`（受理時だけ非0） |
| 拒否のerror（Busy/InvalidBuffer等） | `EspUsbTransferError`：`NotSupported`、`NotMounted`、`BadArgument`、`NotAligned`、`NotDmaCapable`、`Busy`、`TransferFailed` |
| 完了の結果（Success/Cancelled/Reset/Disconnected/IoError） | `EspUsbTransferStatus`：`Success`、`BusReset`、`Stopped`、`TransferError`、`Cancelled` |
| `cancel(ticket)` | `cancelDirectTx(id)`、`cancelDirectRx(id)`（id=0は現在の借用） |
| `onTransferComplete` | `onDirectTxComplete`、`onDirectRxComplete`（USB workerで呼ぶ） |
| 終端`Continue`/`EndWithShortPacket` | `EspUsbTransferTermination`（借用）、`EspUsbStreamTermination`（copy write） |
| `configured()` | `ready()`。ほかに`started()`、`suspended()` |
| `detach()` | 実装しない。切断は`end()`で行う |
| `begin()`の戻り値`Result` | `bool`と`lastError()`（`esp_err_t`）。`end()`は`esp_err_t` |
| クラス別config（`CdcConfig`等） | `EspUsbBulkStreamConfig`（CDC/Vendor共通）、`EspUsbAudioConfig`など。`configure()`は`esp_err_t` |
| 暫定TinyUSB adapter | 撤去済み。本体内の独自coreと保守DWC2 fork（[共通core](V3_CORE_API.ja.md)） |
| P4で隣接変数とcache lineを共有するRX buffer | 許可しない。借用bufferは64 byte境界、RXの容量はMPSと64の倍数 |

## 守る契約と、実測で選ぶもの

**先に守るもの**（実装方法に問題があれば直すが、契約自体は弱めない）：

- 使わないclassを参照しない依存の向き。
- 資源の所有者を一つに決める。
- 拒否と受理を区別する。
- 転送の終了時に、bufferを一度だけ返す。
- 切断・停止時に回収する。
- 設定の失敗を見えるようにする。

**実測で選ぶもの**：既定の容量、queueの深さ、worker/pollingの方式、通知をまとめる頻度、FIFOの配分、
DMA bufferの調達方法、直接転送の上限と分割の方法。

黙って通知を落とすなど、所有権を曖昧にして速くしたことにはしない。

## 依存の向き

```text
利用者 → Device / Function (HID, CDC, Vendor, …)
                       ↓
            descriptor / endpoint / transfer契約
                       ↓
                EP0・bus state・controller
                       ↓
               ESP PHY / DWC2 / RTOS

任意helper (SD, OTA, IP, keyboard layout等) → 必要なFunction
```

- Deviceは登録されたFunctionだけを見る。全classを列挙するswitch、全classへのextern参照、
  全class分の最大配列、静的な自動起動は持たない。
- DeviceはFunctionより長く生きる。DeviceとFunctionはcopyもmoveもできない。
- FunctionはconstructorでDeviceの侵入型listへ登録する。登録にheapは使わない。
  `begin()`中以降の登録・解除は拒否する。登録できなかったFunctionは、黙って有効扱いにしない。
- Functionを破棄する前に、`end()`を成功させる。宣言はDevice→Functionの順、終了は`end()`→Function破棄→Device破棄の順。
- 各classとhelperを別のtranslation unitに分ける。共通headerからSD、IP、OTAの実装headerをincludeしない。
  library全体は`dot_a_linkage=true`でarchiveとしてリンクし、使わない.oを取り込まない（[実測](V3_EXPERIMENTS.ja.md#archiveリンクの小規模実測)）。
- `begin()`は、登録listからinterface/endpointの要求を集め、能力表と照らして全体を計画する。
  descriptorの長さを数える処理と書き出す処理を分け、同じ構成から両方を作る。
  最後まで成功してからattachする。失敗したら、PHYやtaskも含めて取得の逆順に戻す。

当時の移行方針（完了済み）：TinyUSBの暫定adapterは内部だけに置き、最終的に独自core/DWC2へ置き換える。
恒久的なbackend選択APIは作らない。経緯は[共通core](V3_CORE_API.ja.md#経緯の記録)。

## lifecycleと設定

| 状態・操作 | 契約 | 実装の注記 |
| --- | --- | --- |
| 停止中 | 宣言・構成・bufferの指定・callbackの設定を変えられる。descriptorと転送資源は未確保 | |
| `begin(config)` | 検証 → 確保 → endpoint/descriptor準備 → runtime開始 → attach。成功は列挙の成功を意味しない | 戻り値はbool、理由は`lastError()` |
| 開始中 | 構成は固定。構成済み、休止、実効速度は別々に照会する | `ready()`、`suspended()` |
| bus reset / 構成解除 | Hostの設定と転送の世代を更新し、旧転送を終わらせる。Functionの構成は保つ | 借用は`BusReset`で返る |
| `end()` | 新規受付を閉じ、切断、DMA停止・取消し、完了の回収、worker停止、資源の解放。成功して戻った後はcallbackもDMAもbufferに触れない | 戻り値`esp_err_t`。USB worker内からは`ESP_ERR_INVALID_STATE` |
| `begin()`の失敗 | 停止状態へ戻し、失敗理由を返す。次の`begin()`を許す | |
| `end()`の失敗 | 停止を証明できないbufferは解放しない。timeoutだけで停止扱いにしない | runtimeが止まらなければ`ESP_FAIL`を返し、資源を保持する |

- 容量、class数、descriptor、転送modeの変更は停止中だけ。
  「実行時設定」とは、**再ビルドせずに`end()`→設定→`begin()`できること**を指す。接続したままdescriptorを書き換えることではない。
- protocol上の動的状態（Audioのalternate/rate、MSCの媒体状態、CDCのline codingなど）は稼働中に扱う。
- 稼働中にringを広げる機能は、必要性が実測で出るまで作らない。

寿命とcopyの方針：

- 数値の設定は値として保持する。文字列とdescriptorは`begin()`で必要量をcopyし、成功後は元の変数を解放できる。
- 大きなユーザーbufferとcallbackのcontextは借用する。借用期間をAPIごとに明記する。
  classのringは`end()`の成功まで、個々の転送bufferはその転送の完了通知まで保持する。
- 自動確保は`begin()`の時。転送の途中でheapを確保しない。
- ユーザーbufferを渡されたら、容量・alignment・メモリ種別を検証する。知らないうちに自動bufferへ置き換えない。

## 共通の転送契約

まずVendorで確かめ、CDC/MIDIのbyte stream、HIDのreport、Audioのframeへ意味を写す。
すべてを同じ`write()`へ押し込まない。名前は[現在の実装との対応](#現在の実装との対応)を参照。

| 経路 | 戻り値と寿命 |
| --- | --- |
| copy write | copyして受け入れたbyte数。partialを許す。戻った後は元bufferを再利用できる。wire上の完了ではない |
| 借用送受信 | 受理なら非0のid、拒否ならerror。拒否したときは借用せず、通知もしない |
| 完了通知 | id、buffer、要求長/実長、statusを渡す。一度だけ。相手アプリが読んだことまでは保証しない |
| 取消し | 取消し要求の受理とbufferの返却は別。完了と競合しても結果は一つ。DMAが止まる前には返さない |
| stream RX | `available`/`read`。満杯ならbulkの受信を止めてbackpressureをかける。黙って上書きしない |
| 借用RX | 利用者がbufferを投稿し、完了通知で返す。次の受信には再投稿が必要。short、ZLP、切断も同じ経路で返す |
| callback RX | pointerはcallbackの中だけ有効。保持したいなら借用RXを使う |
| timeout | 待ちを打ち切っても、bufferの返却にはならない。生pointerの寿命を隠すwrapperは作らない |

- 借用送信はstrictにする。条件に合わないbufferを黙ってbounce copyしない。
- TX/RXのcache操作はdriverの責務。利用者にSoCごとのcache flushを求めない。
- 容量を表す語を分ける：`capacity`（保持できる量）、`transferBytes`（一回に投稿する量。内部では分割してよい）、
  MPS（wire上のpacket長）。旧TinyUSBの65535/32768を公開の上限に引き継がない。黙って切り詰めない。
- bulk INの終端は転送ごとに明示する。`EndWithShortPacket`は、MPSの倍数長なら必要なZLPまでを一つの転送とする。
  bulkはbyte streamで、`write()`一回とHostのread一回の境界は一致しない。Audio/VideoのISOにはbulkのZLP規則を使わない。

## context、通知、競合

- class処理と完了通知はUSB worker（FreeRTOS task）で行う。ISRは最小限のevent生成だけ。
- USB callbackはISRではないが、block、重い計算、`end()`、構成変更は禁止。
  同じendpointへの次の投稿と、他taskへのsignalは許す。ライブラリは内部lockを持ったまま利用者callbackを呼ばない。
- 投稿したcallbackをその場（inline）では呼ばない。ただし別coreのworkerが、呼出し元が戻る前に完了を処理することがある。
  利用者は投稿の前にcontextを準備する。返ったidの代入完了にcallbackを依存させない。
- `end()`はアプリのtaskから呼ぶ。同時の投稿を閉じ、進行中のcallbackの終了を待つ。
- 同じDeviceの設定とlifecycleの操作は、利用者が直列化する。
- 完了をqueueのあふれで捨てない。bus reset後の古いeventを新しいendpointへ渡さないよう、世代を持つ。

## CH32RVとの当時の比較（2026-10-26）

当時のESP v2とCH32RVの差と、v3の出発案。現在の比較は[CH32RV調整メモ](V3_CH32RV_API_ALIGNMENT.ja.md)を参照。

| 論点 | 当時の差 | v3の出発案 |
| --- | --- | --- |
| Device + Function | ESPはClass、CH32はFunction。どちらもconstructorで関連付け | 名前より意味をそろえる。登録・begin・endを共通化し、ESPのruntime optionは別構造 |
| bufferの設定 | ESPはCFG、CH32のCDCは`setBuffers` | class別configで容量と借用bufferを指定し、拒否をerrorで返す |
| direct TX | ESPはbuild切替でalignment制限あり、CH32はbounceも可能 | strictな借用送信とcopy writeを区別する |
| 完了 | ESPはUSB task、CH32は`deliverEvents`でbyte数を合算 | 転送単位の終了と、まとめてよい通知を分ける |
| RX | ESPはcallback中のpointer、CH32は外部buffer/保留あり | stream、callback中の借用、投稿bufferの返却を分ける |
| error | esp_err_t/bool/lastErrorが混在 | begin/configure/投稿は結果を直接返す。`lastError`だけを転送結果の根拠にしない（並行転送で上書きされるため） |
| 能力の照会 | ESPはstaticな容量getterが多い | 要求値・実効値・controllerの能力をinstanceのAPIで分ける |
| descriptorの拡張 | 内部TinyUSB型に依存する入口がある | class非依存のSETUP/応答/descriptor writerに閉じる |

## 現在のAPIでの利用例

当時の構文案（`CdcConfig{...}`、`trySubmit`など）は採用しなかった。現在のAPIでは次のように書く。

```cpp
#include <EspUsbDevice.h>

EspUsbDevice usb;
EspUsbDeviceHidKeyboard keyboard(usb);
EspUsbDeviceCdcSerial serial(usb);

void setup() {
  EspUsbBulkStreamConfig cdc;
  cdc.rxCapacity = 512;
  cdc.txCapacity = 1024;
  if (serial.configure(cdc) != ESP_OK) return;  // 停止中に設定する

  EspUsbDeviceConfig config;
  config.product = "Keyboard";
  if (!usb.begin(config)) {
    Serial.println(usb.lastErrorName());
  }
}

void loop() {
  if (usb.ready()) serial.println("ready");
}
```

設定を変えるときは、`end()`を成功させてから`configure()`と`begin()`をやり直す。
借用送信の例は[Vendorガイド](V3_VENDOR_API.ja.md)を参照する。

## 能力表とEP0の注意（当時の記録）

SoCの能力はportごとの表にし、runtimeのP4専用分岐をS31へ広げ続けない。
現在のDWC2の表は、S2/S3/P4 FSがendpoint番号7（EP0を含む）、IN 5本（EP0を含む）、FIFO 256 word、
P4 HSが16/8/1024。validatorとbulk/ISOのFIFO予算も同じport表を参照する（[統一の記録](#endpoint番号6と能力表の統一)）。
数字だけで追加endpointを許可せず、EP0、RX FIFO、DMAのendpoint情報、各IN FIFOを同じ計画で数える。
HS controllerはFSへfallbackし得るので、FS/HS両方のdescriptorと構成の可否を用意する。
S31はHS portの番号がP4と同じとは限らず、Core、PHY、DWC2、boardの配線を新たに検証する。

（2026-10-31追記：P4/P4XのHS直結を実機で確認した。S2は引き続きcompileのみ。[P4の記録](V3_P4_VARIANTS_AND_BENCH.ja.md)。）

EP0では次を独立に試験するとした：新SETUPによる前の要求の取消し、data/statusの方向、
SET_ADDRESSの適用時点、STALL後の復旧、GET_DESCRIPTORの短い読出し、SET_CONFIGURATION/SET_INTERFACE、
reset/suspend/remote wakeup。class control handlerへDWC2のregisterを公開しない。

実験の一覧と採用基準は[実験台帳](V3_EXPERIMENTS.ja.md)、作業の状態は[引き渡し](V3_HANDOFF.ja.md)を参照する。

## endpointの記録

以下は実装後の確認記録。

### 専用TX FIFOとendpoint番号

DWC2のIN FIFO番号は、選択済みIN endpoint maskでEP0からの順位を使う。
USB endpoint番号に空きがあってもFIFOは0から詰め、EP0はFIFO 0へ固定する。
割当て・endpoint activate・個別flushは同じ対応を使う。maskは開始時に決まり、転送中に組み替えない。
追加のmap配列・transfer record field・heap取得はない。
Audio＋CDCでIN endpoint 1/2/4/5を使う構成を実測し、従来のendpoint番号=FIFO番号ではCDC INが進まない問題を修正した。
詳細・検証範囲は[Audio＋CDC](V3_AUDIO_API.ja.md#audiocdcの同時通信とtx-fifo割当て)を参照。

### IN endpoint上限の開始時拒否

2026-10-29、独立`controller_capacity`でS3の過大な構成を確認した。
2 CDC＋MSC、3 CDC、2 CDC＋2 MSCは、いずれもbeginがESP_ERR_INVALID_SIZEで拒否する。
上位のvalidateControllerEndpointsがdescriptor構築中に検証するため、IRQ/worker/PHYを開始せず、
Functionの転送資源も残らない。DescriptorsOnlyもこの検証を通すので、同じ構成は拒否する。
当初の「open時にしか確認しない」という見立ては、上位validatorを見落としていたため訂正した。

3構成を各3回拒否し、heapは各構成で一致。余分なFunctionを削除した後は保持した2 CDCを開始でき、
2 port×5長さ×3構成の30往復に成功。1 passed、30.53秒、2 logs STOPPED/panicなし。
原本`/tmp/pytest-embedded/2026-10-29_03-31-24-352616/`、記録`.pytest-results/controller-capacity*`。
初回probeはDescriptorsOnlyなら受け付けると誤って期待したため失敗し、そのログも残した。
今回は製品f5d1571を変更せず、既存の拒否と修正後の再開始を試験で保証した。

IN個数上限とendpoint番号上限は別の条件である。S2/S3のvalidatorの番号上限5と、
DWC2 port表のendpoint数7との違いは、この個数試験だけでは解消したと扱わない。
番号6を使う少数endpointの実通信と、能力表の統一は次節で確認した。

### endpoint番号6と能力表の統一

2026-10-29、endpoint_numberはIN/OUTの個数を各1に保ち、番号を5→6→1→6へ変える。
修正前は5で通信するが6をESP_ERR_INVALID_SIZE、IRQ開始0回で拒否した。
上位validatorはS2/S3の番号を5までに制限し、下位のport表はEP0込み7番号を持つため不一致だった。
validator、bulk double-buffer予算、ISO IN FIFO予算の重複したSoC定数を削除し、
EspUsbDwc2Portのep_count/ep_in_count/otg_dfifo_depthを共通の参照元にした。
AutoはHS優先、FS-onlyでのdetached HS inspectionは従来どおりFSの物理予算を使い、
実際に存在しないcontrollerの起動はruntimeが拒否する。

S3の番号6でIN/OUT双方の実通信に成功した。4回の構成で20往復、各番号はFIFO 1へ詰め、
外部ring guardを維持。番号7はIRQ前に拒否した。IN個数の上限4は増やさず、
過大3構成のUSB開始9回・metadata3回の拒否、heap一致と構成修正後の2 CDC 30往復も維持した。
新規2件は2 passed、81.91秒、4 logs STOPPED/panicなし。
原本`/tmp/pytest-embedded/2026-10-29_03-39-37-170682/`、記録`.pytest-results/endpoint-number*`。

関連control/descriptor/allocation/Audio＋CDC 4件も4 passed、199.67秒、8 logs STOPPED/panicなし。
816開始＋14設定取得点の失敗から回復し、heap346704→346704、DWC2 storage128/192/256/256を維持。
S2/P4 Vendor＋CCID compile、S3/S2/P4各29 sourceの依存/nmと未使用機能除外も成功した。
object field・map配列・heap取得点・転送時の処理は追加せず、開始時に既存の能力表を読む。
原本`/tmp/pytest-embedded/2026-10-29_03-42-10-556094/`、記録`.pytest-results/controller-table*`。
全体収集は42件。この2＋4件を、全42件の一括実行やS2/P4実通信の結果とは扱わない。
