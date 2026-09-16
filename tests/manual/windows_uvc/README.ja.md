# windows_uvc

> English: [README.md](README.md)

**このライブラリが組む UVC descriptor を実機ホストが受け入れ、何もインストールせずに
カメラドライバを当て、フレームを受け取れるか。**

`tests/single/video_descriptor` はホスト無しでバイト列の正しさを保証します。ストリームが
流れるかを言えるのはホストだけで、この環境では最初は流れませんでした。descriptor は正しく、
デバイスは「送信した」と報告し、ホストには 1 バイトも届かない、という状態です。その失敗と
原因を下に書きます。

## 必要なもの

- native USB が Windows PC に繋がった ESP32-S3。**WSL に attach していないこと**
  （`usbipd.exe detach --busid <n>`）
- 書き込み用の別系統 UART。このリグでは開くとボードがリセットされ、それは再接続と同じです。
  開く前にホスト側を読むこと
- キャプチャ用に Windows 側の `ffmpeg`

## 実行

```sh
cd tests/manual/windows_uvc
uv run --with pillow python make_frames.py      # frames.h を作り直すときだけ
printf -- '' > build_opt.h                       # MJPEG 320x240 15 fps
arduino-cli compile --profile esp32s3 --clean . && arduino-cli upload --profile esp32s3 --port /dev/ttyACM3 .
```

キャプチャは WSL から、デバイスを WSL に attach しない状態で:

```sh
ffmpeg.exe -f dshow -vcodec mjpeg -video_size 320x240 -framerate 15 \
  -i 'video=EspUsbDevice Camera' -t 12 -c copy -f mjpeg -y 'C:\Users\Public\uvc.mjpg'
```

`-c copy -f mjpeg` はデバイスが送った JPEG をそのまま残すので、下のバイト単位比較ができます。
コンテナ（`.mkv`）に multiplex すると比較になりません。**失敗しても、それらしいファイルが
できてしまいます。**

変種は `build_opt.h` から選びます。`-DVAR_FORMAT=1` で生成 YUY2、
`-DVAR_WIDTH=` / `-DVAR_HEIGHT=` / `-DVAR_FPS=`、`-DVAR_PID=` で identity を変更、
`-DCFG_TUD_VIDEO_STREAMING_EP_BUFSIZE=` で payload サイズを変更。最後のものはライブラリ全体に
効くので、**毎回 `--clean`** が必要です。

## 計測結果

Windows 11 25H2、build 26200.9457、2026-09-16。ESP32-S3、full speed。

**バインドにインストールは要りません。** 到着から 73 ms で `&MI_00` の子に `usbvideo` が当たり、
クラスは `Camera`、表示名は function 名（`EspUsbDevice Camera`）、compatible ID は
`USB\COMPAT_VID_303a&Class_0e&SubClass_03`。親は IAD により `usbccgp`。DirectShow はデバイスが
宣言した形式だけを報告しました。

```
vcodec=mjpeg  min s=320x240 fps=15 max s=320x240 fps=15
```

**バイト列は届いています。** `-c copy` で 12 秒に 181 フレームを取得し、**その全部が
デバイスの送ったフレームとバイト単位で一致**、順序も連続で欠落も重複もなし。デバイス側は
宣言 15 に対し実測 15.16 fps、51 KB/s、失敗転送ゼロ、キャプチャ終了時にきれいに停止。

**ホストがそれを映像としてデコードできるか**は別の問いで、`-c copy` は答えになりません
（デコードしないので）。`verify_frames.py` は ffmpeg の MJPEG デコーダ（または非圧縮の経路）
を通してもう一度取得し、PNG に書き出して絵を検査します。

```sh
uv run --with pillow python verify_frames.py --format mjpeg
uv run --with pillow python verify_frames.py --format yuy2    # -DVAR_FORMAT=1 のビルド
```

両形式とも 30 フレームがデコードされ、全フレームで 8 本のカラーバーが正しく、ブロックは
ソース 1 フレームにつき 1 セル進み、止まりませんでした。

| | MJPEG 320x240 | 非圧縮 YUY2 160x120 |
|---|---|---|
| デコードしたフレーム数 | 30 | 30 |
| カラーバー | 全フレームで正しい | 全フレームで正しい |
| ブロックの移動 | サンプルあたり 3 セル、停止なし | サンプルあたり 3 セル、停止なし |

（3 セルなのは、スクリプトがフレームレートの 1/3 でサンプリングしているためです。）

### デコード検査を置いた理由

**バイト単位の一致は、非圧縮の絵が間違ったまま通りました。** スケッチは YUY2 を
`Y0 U Y1 V` として書きながら **V を 128 に固定**していて、輝度の階調は完全に正しく、色が
全部違うという状態でした。しかも最初にここへ書いた検査は輝度しか見ていなかったので、それも
通りました。実際の絵は標準のカラーバーではなく、白・黄緑・ラベンダーでした。このスケッチと
`examples/VideoCamera` の両方に同じバグがあり、両方修正し、`verify_frames.py` は実際の色を
比較するようにしたので、同じ間違いは通りません。

一般化すると、**転送の検査と内容の検査は別のテスト**で、前者が通っても後者については何も
言えない、ということです。

### 先に起きた失敗と、それが見えなかった理由

最初のビルドは 1023 バイトの isochronous エンドポイント（full speed の上限）を宣言しました。
列挙は成功、`usbvideo` がバインドし、ホストは streaming パラメータを commit し、デバイスは
15 fps で失敗ゼロと報告し、**ホストには 1 バイトも届きませんでした。** `ffmpeg` が読んだ
バッファには JPEG が入っていません。数十秒後、無関係な EP0 の経路でクラッシュします
（`dcd_event_setup_received` が不正な `DOEPDMA0` を読む）。

ホストが構成した後にデバイス側から読んだ DWC2 の FIFO レジスタが理由を示しました。

| `CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE` | エンドポイント | `DIEPTXF1` | 結果 |
|---|---|---|---|
| 1024 | 1023 バイト | 256 ワード @ オフセット **512** | ホストがストリームを開始できない |
| 1023 | 1023 バイト | 256 ワード @ オフセット **512** | 「成功」して流れるが、ホストには何も届かず、後でクラッシュ |
| 512 | 512 バイト | 128 ワード @ オフセット 98 | 動作する |

S3 の DWC2 は 256 ワード（1 KB）の SPRAM を、受信 FIFO・全送信 FIFO・DMA の endpoint-info 領域で
共有します。endpoint-info（14 ワード）、EP0 の送信 FIFO（16）、受信 FIFO（62）を引くと残りは
約 164 ワード。1023 バイトのエンドポイントには 256 ワード必要です。**割り当ては拒否されません
でした。** `DIEPTXF1` はオフセット 512、使用可能な 242 ワードの外側に設定され、エンドポイントは
どこでもない場所へ送信し、やがて EP0 が使っているメモリを壊しました。

ここから 2 つの変更が出ました。`CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE` の既定を S2/S3 で 512
（P4 は 1023）にし、`EspUsbDevice::begin()` が同じ FIFO 計算で構成全体を検査して、送信できない
カメラとして列挙する代わりに `ESP_ERR_NO_MEM` を返すようにしました。スケッチは commit 時と
stream 開始時に FIFO レジスタを表示します。他のパートで確認するときはこれを見ます。

```
UVC fifo@commit depth=200 rx=62 ep0in=16@226 ep1in=128@98 epinfo=242
```

`ep1in=<ワード数>@<オフセット>`。オフセット + サイズが `epinfo` 未満であること。

### ペーシング

初期のビルドは完了コールバックから次のフレームを投入していました。bulk ストリームには正しい形
ですが、カメラには誤りです。エンドポイントが捌ける速度で流れ、宣言 15 fps に対し 180 fps
になりました。いまは `loop()` が期限で投入し、完了コールバックは数えるだけです。example も
このスケッチも同じ形です。

### デバイスではなくホストが決める 2 つの値

`dwMaxVideoFrameSize` は 153600、つまり 320x240 の**非圧縮**サイズで返りました。descriptor が
MJPEG に 3451 を宣言していてもです。ホストが 0 で probe すると TinyUSB が frame descriptor を
無視して幅 x 高さ x 16 bit から自分で計算します。バッファのヒントにすぎず実害はありませんが、
descriptor の値ではありません。

`dwMaxPayloadTransferSize` も同様に TinyUSB のもので、`CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE` で
頭打ちになります。これがエンドポイントの `wMaxPacketSize` を超えると、ホストは streaming の
alternate setting を開けません。Windows はそれを「キャプチャグラフを構築できない」と報告し、
descriptor については何も言いません。
