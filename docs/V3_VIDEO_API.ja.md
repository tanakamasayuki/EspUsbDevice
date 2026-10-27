# v3 Video: 実行時設定と借用frame

Videoのクラス処理を`EspUsbVideoTransport`へ移し、TinyUSB Video driverを無効にした。
共通EP0/core/DCDは暫定的にTinyUSBを使う。今回も明示的なDevice/Function/config/beginという基本方針を維持する。

```cpp
EspUsbDevice device;
EspUsbDeviceVideo camera(device, "Camera");
EspUsbVideoConfig config;
config.transport = EspUsbVideoTransport::Bulk; // default: Isochronous
config.payloadBytes = 512; // 2-byte UVC headerを含む
camera.configure(config);
camera.setFrameSize(320, 240);
camera.setMaxFrameSize(32000);
camera.onFrameResult([](const EspUsbTransferResult &result) {
  // 全ての終了理由で、ここからresult.bufferを再利用できる。
});
device.begin();
// hostのcommitと、ISOの場合はstreaming alternate選択を待つ。
uint64_t id = 0;
// camera.submitFrame(encodedFrame, encodedBytes, id);
// camera.cancelFrame(id); // 非同期。返却通知までframeを保持する。
```

## 設定と費用

`EspUsbVideoConfig`はFunctionごとに指定する。`payloadBytes`は3..65535 byte、ISOでは最大1024。
ISO packetは省略時にpayload容量から導出し、FS1023/HS1024を上限とする。
`isochronousPacketBytes`を明示する場合も3..1024。ISOの実payloadはstaging容量とpacketの小さい方。
BulkのUSB packetはFS64/HS512で、payloadは複数packetに分割できる。
controllerのFIFO/endpoint能力が足りない構成は開始に失敗する。APIの数値上限は全てのSoCでの成功保証ではない。

begin時に選択Functionの状態と一つのDMA staging bufferだけを確保し、通常frame転送中には確保しない。
frame自体は借用してpayloadごとにstagingへコピーする。複数Videoも各Functionの状態で扱い、単一global ownerを廃止した。
未使用Videoの大きなstatic bufferは持たない。core/task/driver registry等の共通費用は別に残る。

外部stagingは`payloadBuffer`と`payloadBufferBytes`で指定できる。
内部DMA RAM、64-byte alignment、容量を64-byte単位へ切り上げたspanが必要。
利用者がbeginからendまで保持し、driverによる書換え中に触らない。
`payloadBufferSize()`は確保中の論理容量を返し、begin前/end後は0。設定値は`configuration()`で読む。
configure、format/size/fps/最大frame、callback登録は開始後の変更を拒否し、end後に再設定する。

## frameの所有権と通知

`sendFrame()`の通常操作は維持する。詳細な結果が必要な場合は`submitFrame(..., id)`と`onFrameResult()`を使う。
一つのFunctionで一つのframeを借用する。frameは非NULL・非空、MJPEGは最大frame以下、YUY2はwidth×height×2と一致が必要。
成功、Cancelled、BusReset、TransferError、Stoppedの各終了で一度返却する。
`actualBytes`はUVC headerを除く確認済みframe byte数で、abort中など送信量を確定できない場合は`actualBytesKnown=false`。
成功専用の`onFrameComplete()`も維持する。両方を登録するとresult→success callbackの順になる。

`cancelFrame(id)`はworkerへ取消しを依頼する。戻り値が成功でも返却完了ではない。
古いid/0はNotFound。USB転送を止め、古い完了eventを排出してから返却する。
alternate変更と再commitでも旧frameの返却を待つ。onCommitは所有権返却の通知ではない。
callbackはUSB worker上で実行し、軽い処理と次frame投稿にとどめる。
Deviceのend、Functionの破棄、callback差し替えなどのlifecycle操作をcallbackから行わない。
同時投稿は直列化され、既存frameがある場合はInvalidState。frame返却通知前の解放・変更は禁止。

稼働中Functionの外側からの破棄はDeviceを停止し、worker/DMA停止後に状態を解放する。
未登録/二重begin/別runtimeの稼働中beginはInvalidState、startErrorで理由を返す。

## 形式と交渉

一形式・一解像度・一固定frame intervalをadvertiseする。MJPEGまたはYUY2、寸法1..4096、fps1..255。
YUY2は偶数幅かつ固定frame実サイズが必要。未知format、空frame、32-bit bit rate overflowを拒否し、失敗時は旧設定を保持する。
MJPEGの最大frame省略値は非圧縮サイズの1/8。これはencoderの出力サイズ保証ではなく、必要なら明示する。

UVC 1.5の48-byte Probe/Commitと、26/34-byteの短いSETを受け付ける。
Probeは対応する固定形式・intervalとpayload上限へ正規化し、Commitは非対応値を拒否する。
ISOはalternate 0で停止、1で送信。Bulkはalternate 0で送信する。
Bulkの途中payload終端にshort packetが必要な場合はZLPを送る。

## 移行の利点と影響

`CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE`と`CFG_TUD_VIDEO_STREAMING_BULK`は廃止し、同一binaryのFunction設定で選べる。
TinyUSB Videoを有効化する旧build flagはcompile errorにして二重driverを防ぐ。
容量/方式照会はstaticからinstanceへ変わり、callback登録もesp_err_tを返す。
既存の単純なsendFrameと成功通知の操作感は維持するが、中断でもframeを回収する利用側はonFrameResultへ移る。
代償としてbegin時の資源不足と外部DMA領域の契約を扱う必要がある。

## 検証範囲

独自driver初回の`video-driver-initial.xml`は関連2件成功（161.26秒）。
従来のVideo設定/metadata heap回復、257/511/512/1021 byteのISO転送と成功通知、
未登録Functionの拒否、64 KiB frame送信中の破棄、descriptor構築を確認した。

2026-10-27の独自Video driver最終関連試験: `video-driver-cancel.xml`、
**7 passed、0 skipped、0 unfinished、212.48秒**。
ISO/Bulk各64/65/512-byte payloadの6構成を同一binaryの実行時APIで選び、
各構成で4 frameの全byte一致、成功結果のpointer/id/byte数、64 KiB frameの取消しと古いticket拒否、
取消し直後の再投稿、送信中破棄によるStopped一度だけの返却を確認した。
65-byte構成は64-byte alignment・128-byte以上の外部DMA領域を利用する。
既存の独立descriptor probeを含む。14 serial log全てSTOPPED、panicなし。
serial原本は`/tmp/pytest-embedded/2026-10-27_01-53-40-022879/`、コピー/JUnit/transcriptは`validation/.pytest-results/video-driver-cancel*`。
VideoOnlyとDeviceOnlyの最小buildも成功し、nmでTinyUSB Video symbol不在、
DeviceOnlyには独自Video実装もリンクされないことを確認した（`*-video-nm.txt`）。
通常収集は25件。全体の直近成功はNCM時点の19件で、今回の関連7件と区別する。
Videoの確保失敗・複数Function・不正control・cancel/reset/recommit競合は後続の試験対象。
共通EP0/core/DCDと他SoC/HSは未完了であり、v3全体の完成ではない。

PC/OS、JPEG decode、複数Videoの実通信、他SoC/HS、cancel/reset/recommit競合の網羅は未確認。
