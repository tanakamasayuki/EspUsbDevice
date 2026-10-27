# v3 共通coreとEP0の独自化

TinyUSBの`device/usbd.c`をビルド対象から削除し、`EspUsbDeviceCore`へ置き換える。
クラス実装と共通coreは独自側で扱い、DWC2・protocol型・一部helperは暫定依存として残す。
一つの独自coreだけを起動し、旧coreへfallbackする二重backendは持たない。

## 設定と資源

```cpp
EspUsbDevice device;
EspUsbDeviceVendor vendor(device);
EspUsbDeviceConfig config;
config.eventQueueDepth = 16;
if (!device.begin(config)) {
  // device.lastError()で設定不正とメモリ不足を区別する。
}
```

`eventQueueDepth`は非ゼロのuint16_t。旧`CFG_TUD_TASK_QUEUE_SZ`は拒否し、この設定へ移す。
必要容量の確保に失敗すればNoMemで開始を失敗させる。型の上限は全機種での確保成功保証ではない。
同一binaryでend→設定変更→beginできる。キュー深さは待ち時間・処理速度の保証ではなく、
割込みイベントの一時保持量である。深さを小さくすると費用は減るが、同時転送の多い構成では過負荷になりやすい。

core状態、選択configurationのinterface/endpoint map、event queue storage、EP0のDMA packetを
begin時の一つの内部DMA領域に確保する。EP0 packetは64-byte境界でmetadataから分離する。
FreeRTOS queueもその領域を利用し、通常の要求・転送・取消し・過負荷回復中にheap確保しない。
DMA停止とクラス終了後に領域を解放する。FreeRTOS自身のtask TCB等の回収時点とは区別する。

以前の固定interface map、EP0 staging、event queueの全構成向けstatic領域を廃止する。
代償としてbegin時にcore領域の確保失敗が増えるため、失敗点と回復の試験対象へ加える。
通常のDevice/Function/config/beginとread/write等の操作は維持する。

## EP0と標準要求

EP0はSetup/Data/Statusを明示して管理し、受信データの実byte数をクラスへ渡す。
INの短い返信と末尾ZLP、OUTの容量、成功していない転送結果、status方向を区別する。
新SETUPのserialとbus世代をeventへ付け、旧dataが新しい要求へ転送を投稿しないようにする。
先に完了した旧statusのACK callbackは、新SETUPが到着済みでも適用する。
SET_ADDRESSのhardware/status armは現在のDWC2 adapterへ委譲する。

device/interface/endpointの標準要求を方向・値・index・長さで検証する。
未対応configurationへの要求を拒否しても、稼働中の正しい構成は解除しない。
SET_CONFIGURATIONの解除と同じ値の再選択では、DMAを止め、クラスの所有権を返し、再初期化する。
Hostにadvertiseしていないremote wakeup featureや、EP0/ISOのHalt設定は受け付けない。

`sendControlResponse(..., 0)`は、Hostの要求長が非ゼロのINなら長さ0のdataを返す。
要求長0のno-data要求ではstatusを返す。従来の「長さ0なら常にstatus」では非ゼロINのdata段階を
飛ばしてしまうため、両者を分けた。短い返信はHost要求より少ないbyte数でも正常終了する。
OUTで要求長を保持できない受信領域は拒否する。

## eventと所有権

成功した完了eventを失ってborrowed bufferが永久に残ることを防ぐため、queue過負荷では
DMA停止、クラスresetによる所有権返却、旧eventの回収、disconnect/reconnectを行う。
従来のfence予約は非blockingで、満杯ならworker jobから再試行する。
resetが割込みで通知済みの場合は、workerがresetを処理するまで旧構成のproducer jobを進めない。

通常のクラス完了・reset/end通知はUSB worker上で扱う。
現在の内部class/DWC2接続は暫定C ABIを用い、TinyUSB由来の型名・関数名を残している。
これを最終公開APIとして追加したわけではなく、残るDWC2/helperの置換後に整理する。
`startTinyUsb`等の旧backend名を含む公開設定も最終整理の対象である。

## 検証と残り

初回の独自coreではVendor、descriptor、複数Videoの関連3件が成功した
（`core-initial.xml`、253.37秒、serial 6本STOPPED、panicなし）。

## 共通coreの関連検証

2026-10-27。`core-controls-related.xml`は**5 passed、0 skipped、0 unfinished、323.22秒**。
control_core、712取得点の確保失敗、cancel_fence、cancel_lifecycle、video_controlを実行した。
core用private取得点はbegin時の一つで、全失敗点で回復・終了時の未回収0とheap回復を確認した。
control_coreはキュー深さ1/4/16/31で、空・短い・64-byte境界を跨ぐIN/OUT、19件の不正標準要求、
configuration解除と同値再選択、借用返却、意図的なqueue過負荷と自動再列挙後の正常通信を確認した。
各構成でVendor control 20回（IN11、OUT7、no-data2）、借用TX成功2・reset返却3。
FreeRTOSの自己削除taskのTCBはidle taskが回収するため、heap比較は各終了後10 ms待って行う。
10 serial log全てSTOPPED、panicなし。原本`/tmp/pytest-embedded/2026-10-27_03-16-42-737527/`。
コピー/JUnit/transcriptは`validation/.pytest-results/core-controls-related*`。

`core-standard-final.xml`は1件成功（81.61秒）。GET_DESCRIPTORの長さ0/1/8/18/64も追加した。
続いて未使用bootstrap/FIFO sourceを除去した`core-no-legacy-helpers.xml`も1件成功（52.57秒）。
各2 serial logで停止とpanicなしを確認し、nmで独自coreの存在と旧core state/FIFO symbol不在を確認した。
現在のvendor選択は24 file / 2 compiled sourcesで、コンパイルするTinyUSB sourceはDWC2関連だけ。
ABI/header依存を含む全撤去はまだ完了していない。


新SETUPと旧完了の全到着順、OUTの物理的なshort/overrun、suspend/resume・remote wakeup、
各クラスの転送中SET_INTERFACE・HALT/CLEAR_FEATUREでのPID/所有権回復は追加検証・実装の対象とする。
特にaltを実装しないクラスへの共通fallbackは現在alt 0のACKを扱い、
転送中再選択に必要なクラス別回復契約はまだ整理していない。
DWC2、残存helper・protocol定義、内部ABI、他SoC/HS、PC/OSの確認は未完了。

要求の基準は[USB-IF USB 2.0 specification](https://www.usb.org/document-library/usb-20-specification)。
実機試験は独立した`validation/`と標準pluginのcomboを用い、旧test/sampleを合否基準へ戻さない。

## 独自coreでの全体試験

2026-10-27、実装`ce6a444`。標準`--combo all`の`runtime-core-all.xml`は
**27 passed、0 skipped、0 unfinished、1743.12秒**。
712取得点の確保失敗/回復、独立coreの4キュー深さ、過負荷と自動再列挙、各既存クラスとVideoの全経路を一括実行した。
54 serial log全てSTOPPED、panicなし。
原本は`/tmp/pytest-embedded/2026-10-27_03-35-23-682254/`、コピー/JUnit/transcriptは
`validation/.pytest-results/runtime-core-all*`。Core 3.3.12 / Host 2.10.1 / plugin 1.10.0、S3–S3のみ。
TinyUSB manifestは24 file / 2 compiled sourcesで照合成功し、旧test/sampleは変更していない。

この成功は全標準要求・全競合順序や他SoC/HSの検証完了ではない。
転送中のSET_INTERFACEとHALT/CLEAR_FEATUREにおけるクラス別PID・所有権回復、
新SETUPと旧status/dataの到着順、物理short/overrun、suspend/remote wakeupは引き続き対象。
残るDWC2・ABI/header依存、公開APIの最終整理、出荷exampleとOS互換性も未完了。
