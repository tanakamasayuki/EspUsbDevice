# v3 DFU: runtime転送領域と独立したinterface

2026-10-27。DFU Runtime/DownloadのTinyUSB classと固定global bufferを独自driverへ置き換える。
既定のOTA更新はDevice + Dfu + Device.beginで使い、転送サイズはconfigureで指定する。
共通EP0/coreとDWC2も本体内部で保守する。

```cpp
EspUsbDevice device;
EspUsbDeviceDfu dfu(device); // Download, built-in OTA
void setup() {
  EspUsbDfuConfig config;
  config.transferBytes = 4096;
  if (dfu.configure(config) != ESP_OK) return;
  dfu.onProgress([](size_t written) { /* bytes accepted so far */ });
  dfu.onError([](EspUsbDfuStatus status) { /* retain the failure */ });
  device.begin();
}
```

transferBytesは64..65535 byte、既定1024。最大値はUSB control要求のwLengthによる。
transferSize()はdescriptorの要求値、transferCapacity()は確保したDownload領域のbyte数。
後者はbegin前/終了後/Runtimeで0。transferSizeはstaticからinstanceへ変わる。
外部transferBufferはtransferBytes以上のbyte配列で、開始から終了まで有効に保ち、直接書き換えない。
CPU copy用なのでDMA alignmentを要求しない。Downloadのbufferは一度だけ確保し、blockごとのheap操作はない。
Runtimeは同じdescriptor上のtransferSizeを宣言するが、block領域を確保しない。

configureとcallback/backend/restart設定は停止中だけ受け付け、稼働中はInvalidState。
サイズとpoll timeoutの範囲外はInvalidSize、不完全なbackendやpointer overflowはInvalidArg。
開始時の取得失敗はDevice.lastError/startErrorへNoMemoryを伝え、rollbackで領域を回収する。
Device.end→configure→Device.beginでサイズを変更でき、ビルドflagやclean rebuildを使わない。

## 保存先を選ぶ

EspUsbDfuBackendのdownload/manifestを両方指定すると、OTA以外の保存先を使う。
外部flash、別processorへの更新転送、アプリが検証・適用を管理するimageなどを想定する。
空のbackendは既定OTAへ戻す。RuntimeへのDownload backend指定は拒否する。

```cpp
EspUsbDfuBackend backend;
backend.download = [](uint16_t block, const uint8_t *data, size_t bytes) {
  // Copy/consume before returning; data must not be retained.
  return storeBlock(block, data, bytes) ? EspUsbDfuStatus::Ok : EspUsbDfuStatus::Write;
};
backend.manifest = []() {
  return verifyAndActivate() ? EspUsbDfuStatus::Ok : EspUsbDfuStatus::Verify;
};
backend.abort = []() { discardPartialUpdate(); };
dfu.setBackend(backend);
```

callbackはUSB worker上で同期実行する。downloadのpointerはcallback中だけ有効。
一部しか保存できなかった場合も成功とは返さずstatusで拒否する。無効なstatus値はUnknownへ正規化する。
abortは任意で、保存処理を開始したtransactionの失敗・ABORT・reset/end時に一度呼ぶ。
保存前に取り消したblockや成功したmanifestの後には呼ばない。manifest後の検証・適用・再起動はbackendの責任。
既定OTA専用のonComplete/restartWhenCompleteはcustom backendへ適用しない。
onProgressとwrittenは成功したdownloadの累積byte数、onErrorは型付きstatusを受け取る。

既定OTAのonCompleteはfalseで受理を拒否し、起動先を実行中のpartitionへ戻す。
restartWhenCompleteは既定true。falseならアプリが再起動する。
manifestationTolerantは既定falseで、成功後はUSB resetを待つ。
trueならmanifest完了後のGETSTATUSでdfuIDLEへ戻り、別transactionを開始できる。
同時に複数のDFU interfaceを公開でき、状態・領域・backendを共有しない。
既定OTA同士は同じ保存先へ並行して書かないよう、transaction中の利用者を一つに制限する。

## 制御と寿命

[USB-IF DFU 1.1](https://www.usb.org/sites/default/files/DFU_1.1.pdf)の6章と付録Aを参照して状態遷移を実装する。
受信したblockはdata/status段階が完了するまで保存せず、GETSTATUSのACK後にbackendへ渡す。
block番号は0から順に16 bitで循環する。番号の飛び、最大長超過、不正な要求はdfuERRORとなり、
HostがCLRSTATUSしてから再開する。Runtimeでは不正DFU要求でdownload状態へ遷移しない。
GETSTATUSのpoll値はdownloadPollMs（既定1）/manifestPollMs（既定250）で24 bitの範囲。
Runtime DETACHはACK後にcallbackを呼ぶ。callbackがなければ従来どおりROM loaderへ再起動する。
callbackが処理しないままdetach timeoutを迎えた場合は、次の要求時にappIDLEへ戻す。

利点は使わないblock領域の省略、再ビルド不要のサイズ指定、複数interface、保存先とUSB状態の分離。
不利益は開始時の動的確保とcallback契約の追加、state管理の費用である。
旧CFG_TUD_DFU_XFER_BUFSIZEはconfigureへ、class指定のtransferSizeはinstance呼出しへ移す。
onErrorの引数はuint8_tからEspUsbDfuStatusへ変更する。他のcallback呼出し方は保ち、setterはesp_err_tを返す。

この段階ではUpload、複数alternateによる領域選択、非同期flash callbackを実装しない。
Runtimeのみでも共通driverのDownload/既定OTA処理はリンクされ得るため、flash費用まで完全な分離とは扱わない。
P4/S2/S31/HS、dfu-util/OS、実OTAの検証と再起動、block番号の周回、EP0途中切断は実機で別途確認する。


## S3–S3の実測結果

`dfu-runtime-related.xml`は**2 passed、0 skipped、0 unfinished、74.27秒**。
64/129/4097/65535 byte、内外buffer、独立2 Download + Runtime、GETSTATUSとblock順序、
不正要求のSTALL/CLRSTATUS、write/manifest失敗、ABORTとend時の一度だけの回収、
manifestation tolerantとreset待ち、DETACHのtimeout復帰を確認した。metadata反復のheapも回復した。
502取得点の確保失敗/回復は338520→338520 byte、両case両board STOPPED、panicなし。
serial原本は`/tmp/pytest-embedded/2026-10-26_19-25-20-261549/`、コピーとJUnit/transcriptは`validation/.pytest-results/dfu-runtime-related*`。
DFUのみ/Deviceのみの最小build/nmで、旧TinyUSB DFUと未使用独自DFUの不在を確認した。
試験は公開IDF Host APIのRAM backendで行い、conftestや独自pytest hookを使わない。
Coreのenum filterが有効なため、Hostは対象VID/PIDのfilterを明示する。
通常収集は17件。関連2件の後に行った全17件の結果は下記を参照。


## DFU置換後の全体確認

2026-10-27。標準`--combo all`は**17 passed、0 skipped、0 unfinished、841.47秒**。
全17 caseで両board STOPPED、34本のserialにpanicなし。502取得点の確保失敗/回復、
DFUの最大block/複数interface/manifestationと、従来のAudio/CDC/Vendor/HID/MIDI/MSC、
descriptor/registryと取消し・転送境界を含む。
serial原本は`/tmp/pytest-embedded/2026-10-26_19-27-27-183733/`、コピーとJUnit/transcriptは`validation/.pytest-results/runtime-dfu-all*`。
DFUのみ/Deviceのみの最小build/nmも成功。TinyUSB treeは51 file / 15 compiled sourcesで一致、
旧tests/examplesはmainと同一で、mainはv3の祖先。実OTAの適用・再起動と他SoC/HSは未検証。


## DETACH受付APIの仕様案

2026-10-30、**未実装・レビュー用**。上記の現行void callbackとは別の提案である。
CH32RV側から本案のAPIを実装し、X035C8実機試験を通したとの返答を受領。
以下の受付・移行分離に合意した。ESP側の実装・検証は未着手である。
ESPも要求受付と、受付後に実行する移行処理を分ける。

```cpp
// 提案するRuntime用API。現行headerには未反映。
using DetachCallback = std::function<bool()>;
esp_err_t onDetach(DetachCallback callback);
using DetachHandler = std::function<void()>;
esp_err_t setDetachHandler(DetachHandler handler);
```

onDetachは受付判定、setDetachHandlerは既定のROM loader移行を置き換える処理とする。
単なる通知を登録しただけで移行方式が変わる曖昧さを避けるため、後者はhandlerと命名する。
共通化の対象はonDetachの名前と受付契約。ESP固有の移行handlerをCH32RVへ必須にしない。
両方とも登録・解除は停止中のみで、稼働中はESP_ERR_INVALID_STATE、設定は変更しない。
空のcallbackは無条件受付へ、空のhandlerは既定移行へ戻す。Downloadでの登録は
ESP_ERR_INVALID_ARGとして拒否する案。DownloadのonComplete/restartWhenCompleteは変更しない。

| 段階 | 提案する処理 |
| --- | --- |
| SETUP | 形式・対象interface・Runtime状態を検査し、有効なDETACHだけでonDetachを1回呼ぶ |
| false | EP0をSTALL。appIDLEを保ち、移行handlerや再起動予約を実行しない |
| trueまたは未登録 | status段階を用意する。この時点では移行処理を始めない |
| status ACK完了 | appDETACHへ遷移し、handlerがあれば1回呼ぶ。なければ既定ROM移行を予約する |
| ACK前の取消し/reset/end | 受付判定の副作用を巻き戻す機構は持たず、移行handlerと再起動予約は実行しない |

受付判定はUSB workerで短時間に返す。アプリが公開したbusy状態の参照などに限定し、
保存領域の書換え、待機、end、構成変更を行わない。拒否後の再試行は新しい要求として判定する。
移行handlerもworkerで呼び、重い処理やDevice停止はアプリtaskへ引き継ぐ。
handlerを指定したアプリはUSB detachと移行を完了する責任を持つ。単にログを出すだけでは
既定ROM移行は行われない。既定移行の可否にはSoC・boot設定の制約が残る。

USB上の拒否を表すのはSETUPでのSTALLであり、ACK後にboolを返して再起動を抑止する設計にはしない。
[DFU 1.1の5.1節と付録A](https://www.usb.org/sites/default/files/DFU_1.1.pdf)を照合先とする。
現在宣言しているbitWillDetach=1に対応したdetach/attachの責任はhandlerにも及ぶ。
既存のdetachTimeoutMs、要求wTimeout、固定再起動猶予の関係は実装前に別途確定し、
単なるtimeout復帰を移行成功として扱わない。ACK後に予約した再起動とend/resetの競合・取消しも
実装前のレビュー項目とする。今回の案で期限処理まで確定済みとはしない。

移行時は既存のvoid onDetachへ渡した処理をsetDetachHandlerへ移す。
拒否が必要な場合だけbool onDetachを追加する。callbackなしの単純なRuntime利用は変更しない。
利点はHostへの拒否と実際の移行を区別できること。不利益は既存callbackの移行と、
受付後の移行責任を理解する必要があること。

合格条件は拒否時のwire STALL・状態不変・予約なし、許可時のACK後1回だけの移行、
ACK前取消し、拒否後再試行、custom handler、停止との競合。ROM移行そのものは別の実機試験とする。
APIの合意を基に、残る期限・予約取消しの詳細を確定して実装へ進む。
[調整状況](V3_CH32RV_API_ALIGNMENT.ja.md)を参照。
