# EspUsbDevice VideoCamera

> English: [README.md](README.md)

カメラを繋がない USB Video Class カメラです。テストパターンを生成して流すので、対応ボードが
1 枚あれば何も配線せずに動きます。

Windows・macOS・Linux はいずれも標準の UVC ドライバを何もインストールせずに当て、webcam が
並ぶ場所にそのまま現れます（カメラアプリ、ブラウザの `getUserMedia()`、OBS、
`ffmpeg -f dshow`、`v4l2`）。

```cpp
EspUsbDevice device;
EspUsbDeviceVideo camera(device, "EspUsbDevice Camera");

EspUsbVideoConfig config;
config.transport = EspUsbVideoTransport::Isochronous;  // Bulkも選べる
config.payloadBytes = 512;
camera.configure(config);
camera.setFormat(EspUsbDeviceVideoFormat::Yuy2);
camera.setFrameSize(160, 120);
camera.setFrameRate(15);
camera.onFrameResult(...);   // ここでframeがアプリへ返る
device.begin(identity);
```

## まず速度を知ること

制約は isochronous の帯域で、full speed では潤沢ではありません。packet は full speed では 1 ms に
1 つ、high speed では 125 µs の microframe に 1 つ送ります。各 payload には 2 byte の UVC header が
入ります。

| パート | リンク | ISO、既定 512 byte packet | ISO、1023/1024 byte packet | 160x120 YUY2 | 320x240 YUY2 |
|---|---|---|---|---|---|
| ESP32-S2 / S3 | full speed | 510,000 B/s | 約 1 MB/s（FIFO に収まれば） | 約 13 fps | 約 3 fps |
| ESP32-P4 HS port | high speed | 4,080,000 B/s | 約 8 MB/s | 15 fps に余裕 | 約 26 fps |

計算上の上限で、実測ではありません。この例は 160x120 の非圧縮（YUY2）で、15 fps を宣言します。
1 フレーム 38,400 byte、576,000 B/s です。full speed の既定 packet では 1 フレームに約 76 ms
かかるので、実際には約 13 fps で流れます（画像は動いて見えます）。full speed で宣言どおりの
レートが必要なら、フレームを小さくするか MJPEG を使います。

非圧縮を使っているのはエンコーダが要らないからで、大きなフレームを実用にするのは MJPEG です。
ライブラリは `camera.setFormat(EspUsbDeviceVideoFormat::Mjpeg)` と `camera.setMaxFrameSize(bytes)`
で対応します。**JPEG のエンコードはライブラリの仕事ではありません。** 実製品では、すでに JPEG を
出すセンサか、自前のエンコーダからフレームを渡します。大きさが変わる MJPEG には、帯域の予約の代わりに
USB の再送を使う `config.transport = EspUsbVideoTransport::Bulk` も選べます。

## 必要なもの

- native USB を持つ ESP32-S2 / ESP32-S3 / ESP32-P4 ボード
- ホストに繋ぐ USB ケーブル
- `Serial`（115200）用の別の debug 接続

以上です。映像は生成します。

## 期待される出力

```
UVC camera ready: 160x120 YUY2 at 15 fps, 512-byte payloads
Open it wherever your OS lists webcams.
Host committed: frame 1, 66666600 ns interval, payload 512 bytes
Streaming started
Streaming stopped: 412 frames sent, 1 not completed
```

カラーバーと、下端を 1 フレームに 1 コマずつ移動する白ブロックが出ます。動くブロックが要点で、
静止画では「生きているストリーム」と「止まったストリーム」を区別できません。ホストが stream を
閉じて途中で切れたフレームは、成功以外の状態で返り、「not completed」に数えます。

## 真似する価値のある 3 点

**フレームの投入は 1 箇所から、宣言したレートでペーシングする。** 結果のコールバックは最後の
payload が出た直後に呼ばれるので、その中で次を投入するとエンドポイントが捌ける速度で流れます
（v2 で宣言 15 fps に対し実測 180 fps）。`loop()` が期限で投入し、コールバックは数えるだけにします。

**フレームバッファは `onFrameResult` で返るまでドライバのものです。** 送信中に payload ごと
ドライバの staging 領域へコピーされるので、次のフレームを描くのは `frameInFlight()` が false に
なってからです。結果は、フレームの終わり方（`Success`、`Cancelled`、`BusReset`、`TransferError`、
`Stopped`）にかかわらず一度だけ届きます。

**設定の各段階を確認すること。** `configure()` は `esp_err_t` を返し、format の setter は受け付けない
値に false を返し、`submitFrame()` は受け取らなかった理由を返します。`device.begin()` が
`ESP_ERR_INVALID_SIZE` で失敗したら、構成がコントローラに収まらないという意味です。`begin()` は、構成内のすべての
IN エンドポイントが共有の送信 FIFO（S2、S3、P4 の FS コントローラでは 1 KB を全エンドポイントで共有）に収まるか、
endpoint の数、descriptor の大きさを検査します。`payloadBytes` を下げるか、Bulk を選ぶか、ほかの Function を外します。
`ESP_ERR_NO_MEM` は heap の不足です。ライブラリは「送信できないカメラとして列挙する」のではなく起動を拒否します。

## 関連

- [docs/usb-device-guide.ja.md](../../docs/usb-device-guide.ja.md) — 速度、endpoint 予算、
  各ホスト OS からの観測方法
- [v2 tests/manual/windows_uvc](https://github.com/tanakamasayuki/EspUsbDevice/tree/3fed50d0d777f600432b71381faccd93adb9a202/tests/manual/windows_uvc/) — v2 での
  Windows の実測（180 fps の数値を含む）

## 読み方と確認の手順

### 実行前の準備

boardに合うprofileを選びます（既定は`esp32s3`。ほかに`esp32s2`、v1.xのP4は`esp32p4`、
v3.1以上のP4は`esp32p4x`。v3.0は対象外）。診断Serialの接続先も確認します。
[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と
相手の利用可能状態の違いを説明しています。

### コードを追う

スケッチはセンサを読まず、`frame`に画像を生成します。`loop()`の`streaming()`、`frameInFlight()`、
`submitFrame()`を追い、前のフレームが返ってから描き直すことを確認します。どちらのcallbackも
USB workerで動くので、commitは`onCommit`で印だけ付け、`loop()`で表示します。

### APIの使い方と改造時の注意

画素形式・寸法・frame rateを両端で合わせます。YUY2のフレームはちょうど`幅×高さ×2` byteです。
解像度を上げる前に、交渉・フレームの完了・受け手の速度を別々に確かめます。フレームの数やUSB送信の
成功だけでは、PCがすべての画像を表示したことにはなりません。

### 一つずつ確認する

最初は例を変更せずに動かし、ログとホストのカメラアプリを照合します。サイズ・レート・転送方式は
一つずつ変えます。例のエラー表示は、再試行や回復の方針ではありません。

## v2からの変更点

- 転送方式とpayloadの大きさは、`CFG_TUD_VIDEO_STREAMING_*`のbuild flagではなく、実行時に
  `camera.configure()`で選びます（IsochronousかBulk）。
- `payloadBufferSize()`はinstanceごとになり、`begin()`前は0です。スケッチは`device.begin()`の後で
  表示します。
- フレームが返る場所は`onFrameComplete()`から`onFrameResult()`になり、中断したフレームも返ります。
  `submitFrame()`は受け取らなかった理由（`EspUsbTransferSubmission`）を返します。
- commitの表示は、USBのcallbackの中ではなく`loop()`で行います。
- 速度の表は、v3の数え方（(micro)frameごとにISO packet 1つ）に直し、この例がfull speedで出せる
  frame rateも書きました。

Arduino-ESP32 3.3.12で`sketch.yaml`の全profileのcompileを確認しています。このスケッチ自体の
実機試験はしていません。

- [スケッチ](VideoCamera.ino)
- [全サンプル](../README.md)
- [Video API](../../docs/classes/video.ja.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
