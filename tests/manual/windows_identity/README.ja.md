# windows_identity

> English: [README.md](README.md)

**`EspUsbDeviceConfig` の identity 系の各項目は、すでにそのデバイスを見たことがある Windows PC に
何をするのか——そしてどれを変えると「別のデバイス」になるのか。**

ユーザーガイドの [1.8](../../../docs/usb-device-guide.ja.md#18-vidpid-とその周りの-identity) と
[5.2](../../../docs/usb-device-guide.ja.md#52-windows) は以下の読み取り結果から作りました。この
ディレクトリには、それを生んだスケッチと、読み戻したツールがあります。別の Windows build で繰り返せる
ように、です。実際に繰り返してください。Windows は変わりますし、ここの数値はすべて 1 台の PC の 1 日の
記録です（Windows 11 25H2、build 26200.9457、2026-09-16）。

## 必要なもの

- native USB が Windows PC に繋がった ESP32-S3。**WSL に attach していないこと**
  （`usbipd.exe detach --busid <n>`）。attach 中は Windows には USBIP デバイスで、何もバインドされません
- 書き込み用の別系統 UART。**その UART を開くとボードがリセットされる**なら（計測に使ったリグは
  そうでした）、開く*前*に Windows を読んでください。リセットは再接続で、新しい列挙が始まります

## 実行

全項目が `build_opt.h` のスイッチで、スケッチは「何を組んだか」と「ディスクリプタが何と言っているか」を
表示します。function は `VAR_F1`〜`VAR_F3` に登録順で（1 vendor、2 CDC、3 HID keyboard、4 mass storage、0 なし）。

```sh
cd tests/manual/windows_identity
printf -- '-DVAR_F1=1\n' > build_opt.h                 # vendor 単独、PID 0x4090
arduino-cli compile --profile esp32s3 --clean . && arduino-cli upload --profile esp32s3 --port /dev/ttyACM3 .
cd ../../ && uv run python manual/windows_identity/windows_identity.py \
    --instance "VID_303A*PID_4090" --guid "{D4D4D4D4-4444-4444-8444-444444444444}" --since 11:33:30
```

あとは 1 つ変えて繰り返します。`--since`（ローカル時刻、当日）は表示する Kernel-PnP イベントの
範囲で、焼いた時刻を渡します。**毎回 `--clean` を付けること。** `build_opt.h` は応答ファイル経由で
スケッチには届きますが、stale build ではライブラリが再コンパイルされません。空白を含む値は応答ファイル
向けにオプション全体をクォートします: `'-DVAR_PRODUCT="Identity test B"'`。

ツールは、instance パターンに一致する present な全ノードについて、Windows が当てたドライバ、表示名、
bus-reported description（今回デバイスが送ったもの）、hardware ID と compatible ID、INF、到着時刻、
COM ポート名、記録済みの `DeviceInterfaceGUIDs` を出し、続けてその GUID に登録された全 device interface
と状態（`pnputil /enum-interfaces`）、`SetupDiGetClassDevs(DIGCF_PRESENT | DIGCF_DEVICEINTERFACE)`
が実際に返すもの、指定時刻以降の `Microsoft-Windows-Kernel-PnP/Configuration` イベントを出します。
デバイスが健康に見えるのに動かないとき信じるべきは最後の部分です。`Get-PnpDevice` は、開始が保留中の
子にも、開始がすでに失敗した子にも `STATUS OK` を返しました。

## 計測したもの

identity は `303a:4090`、serial `ident-1` に固定、vendor 単独、1 行につき 1 項目、各行はそれを変えた
焼き込みの後に読み取り。動いた行ではドライバの開始は到着から 10〜50 ms でした。

| 行 | 変えたもの | 結果 |
|---|---|---|
| I1 | 基準 | device ノードに `WINUSB`、表示名と bus description は `Identity test A`、hardware ID `USB\VID_303A&PID_4090&REV_0100`、GUID 記録済み、interface 有効・列挙可 |
| I2 | `product` → `Identity test B` | bus-reported description は `Identity test B`。**表示名は `Identity test A` のまま**。再構成イベントなし |
| I3 | `manufacturer` → `Acme Devices` | **どの PnP プロパティにも現れない**（製造元列は INF 由来の `WinUsb Device`） |
| I4 | `deviceVersion` → `0x0200` | hardware ID `REV_0200`。インスタンスとドライバは同じ、再構成イベントなし |
| I5 | `vid:pid` → `1209:0001` | **新しいインスタンス** `USB\VID_1209&PID_0001\IDENT-1`、ドライバ新規インストール（400/410）、GUID 新規記録 |
| I6 | `webusbEnabled` オン | Windows 側は何も変わらない |
| I7 | `msOs20Layout` → Subsets | **ドライバなし**: compatible ID から `USB\MS_COMP_WINUSB` が消え、`STATUS Error problem=CM_PROB_FAILED_INSTALL`、`Device Updated: true` で再構成。Windows は function subset を composite にしか適用しない |
| I8 | `maxPowerMilliamps` 500、`selfPowered` | どの PnP プロパティにも現れない。インスタンスは I7 から回復（再び `WINUSB`、GUID 再記録） |

続けて同じ identity で CDC function を 5 つの形に通しました。vendor クラスがある行では vendor を先に登録:

| 行 | 登録順の function | CDC の子 | COM | その他 |
|---|---|---|---|---|
| C1 | CDC | `MI_00` | **COM16** | 親は `WINUSB` → `usbccgp` に再バインド、子に `usbser`、bus description は `Console`（function 名） |
| C2 | CDC, vendor | `MI_00`、同じインスタンス | COM16 のまま | 新しい子 `MI_02` に `WINUSB`、GUID 記録、列挙可 |
| C3 | vendor, CDC | `MI_01`、**新しいインスタンス** | **COM34** | `MI_00` は `usbser` → `WINUSB` に再バインド、`PortName COM16` を抱えたまま（不活性） |
| C4 | HID, CDC | `MI_01`、C3 と同じ | COM34 のまま | `MI_00` は `WINUSB` → `HidUsb`、`kbdhid` の子が開始 |
| C5 | CDC | `MI_00` | **COM16 に戻る** | |
| C6 | CDC、function 名なし | `MI_00` | COM16 | bus description は `Identity test A`（product 文字列）にフォールバック |

親は C1〜C6 を通じて単一インターフェースだった頃の GUID を持ち続け、`MI_00` は WinUSB や HID の
インターフェースだった間も `PortName COM16` を持ち続けました。どちらにも到達できません。GUID 列挙は
毎回、生きている vendor interface ちょうど 1 つを返し、COM ポートは `usbser` がバインドされた場所に
しか存在しませんでした。

## 結果の読み方

- **新しいデバイスになる項目は 3 つ**: `vid`、`pid`、`serialNumber`。それ以外は同じインスタンスをその場で変えます。
- **ドライバとディスクリプタは接続のたびに読み直されます。** vendor revision の後ろにキャッシュされる
  のは Microsoft OS 2.0 の registry property だけで、ライブラリは revision を set から導出するので
  自動的に動きます。
- **表示名はドライバのインストール時に決まります。** bus-reported description は生きた値、デバイス
  マネージャーの表示名は Windows がドライバをインストールしたときの値で、デバイスを再構成したときだけ
  変わります。
- **子インスタンス、したがって COM ポートは、インターフェース番号でキーされます。** シリアルポートの
  後ろに function を足せば COM 番号はそのまま、シリアルポートを動かせば新しい番号になります。
- **`msOs20Layout` は Auto のまま。** 単一インターフェースに subset を強制するとドライバが当たりません。
