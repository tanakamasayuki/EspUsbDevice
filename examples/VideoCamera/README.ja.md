# EspUsbDevice VideoCamera

> English: [README.md](README.md)

カメラを繋がない USB Video Class カメラです。テストパターンを生成して流すので、対応ボードが
1 枚あれば何も配線せずに動きます。

Windows・macOS・Linux はいずれも標準の UVC ドライバを何もインストールせずに当て、webcam が
並ぶ場所にそのまま現れます（カメラアプリ、ブラウザの `getUserMedia()`、OBS、
`ffmpeg -f dshow`、`v4l2`）。

## まず速度を知ること

制約は isochronous の帯域で、full speed では潤沢ではありません。

| パート | リンク | isochronous 帯域 | 160x120 YUY2 | 320x240 YUY2 | 320x240 MJPEG |
|---|---|---|---|---|---|
| ESP32-S2 / S3 | full speed | 既定 payload で約 0.5 MB/s | 15 fps | 約 6 fps | 余裕 |
| ESP32-P4 | high speed | 約 24 MB/s | 余裕 | 余裕 | 余裕 |

この例は 160x120 の非圧縮（YUY2）15 fps で、1 フレーム 38 KB、576 KB/s です。full speed で
運べるおおよその上限にあたります。非圧縮を使っているのはエンコーダが要らないからで、大きな
フレームを実用にするのは MJPEG です。ライブラリは
`Camera.setFormat(EspUsbDeviceVideoFormat::Mjpeg)` と `Camera.setMaxFrameSize(bytes)` で
対応します。**JPEG のエンコードはライブラリの仕事ではありません。** 実製品では、すでに JPEG を
出すセンサか、自前のエンコーダからフレームを渡します。

## 必要なもの

- native USB を持つ ESP32-S2 / ESP32-S3 / ESP32-P4 ボード
- ホストに繋ぐ USB ケーブル

以上です。映像は生成します。

## 期待される出力

```
UVC camera ready: 160x120 YUY2 at 15 fps, 512-byte payloads
Open it wherever your OS lists webcams.
Host committed: frame 1, 66666600 ns interval, payload 512 bytes
Streaming started
```

カラーバーと、下端を 1 フレームに 1 コマずつ移動する白ブロックが出ます。動くブロックが要点で、
静止画では「生きているストリーム」と「止まったストリーム」を区別できません。

## 真似する価値のある 3 点

**フレームの投入は 1 箇所から、宣言したレートでペーシングする。** 完了コールバックは最後の
payload が出た直後に呼ばれるので、その中で次を投入するとエンドポイントが捌ける速度で流れます
（宣言 15 fps に対し実測 180 fps）。`loop()` が期限で投入し、コールバックは数えるだけにします。

**フレームバッファはコールバックが呼ばれるまでドライバのものです。** コピーではなく payload
単位で読まれるので、次のフレームを描くのは `frameInFlight()` が false になってからです。

**`begin()` の戻り値を見ること。** `ESP_ERR_NO_MEM` は、コントローラの送信 FIFO が、構成内の
ほかのエンドポイントと合わせて isochronous エンドポイントを賄えないという意味です。S2 と S3 は
全エンドポイントで 1 KB の FIFO を共有します。ライブラリは「送信できないカメラとして列挙する」
のではなく起動を拒否します。宣言だけしてしまうと、まさにそれが起きます。

## 関連

- [docs/usb-device-guide.ja.md](../../docs/usb-device-guide.ja.md) — 速度、endpoint 予算、
  各ホスト OS からの観測方法
- [tests/manual/windows_uvc](../../tests/manual/windows_uvc/) — 上の数値の元になった
  Windows での実測
