# v3 DFU: runtime転送領域と独立したinterface

2026-10-27。DFU Runtime/DownloadのTinyUSB classと固定global bufferを独自driverへ置き換える。
既定のOTA更新はDevice + Dfu + Device.beginで使い、転送サイズはconfigureで指定する。
共通EP0/core/DCDは暫定TinyUSBのままである。

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
通常収集は17件。今回の関連2件は直近の全16件成功とは別である。
