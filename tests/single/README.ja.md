# Single Board テスト

> English: [README.md](README.md)

デバイス側ボード 1 枚だけを使うテストです。USB ホスト役のボードは使いません。
いずれも実機にスケッチを書き込んで本物の API を呼び、`OK` / `NG` を出力します。
pytest 側はその判定を読むだけです。

大半はバスではなくチップが必要なだけです。`config.startTinyUsb = false` にして
`begin()` に descriptor を組み立てさせ、その byte 列を検査しています。PC で動かせないのは
ライブラリが Arduino / ESP-IDF のコードだからです。builder が Arduino も TinyUSB も
使わない純粋な byte 組み立てなら、`../unit/ccid_descriptor` や `../unit/midi_descriptor`
のホストコンパイル方式のほうが適しています。CI で回せて、同じ出荷コードを検証できます。

`p4_controller_endpoints` だけは今後もここに残ります。検証している endpoint の上限が
P4 のコントローラそのものの性質だからです。

`peer/` や `loopback/` と同じく `tests/.env` のポートが必要です。

```sh
uv run --env-file .env pytest single/
```

## `compile_smoke`

最初の環境確認用テストで、2 段階で働きます。`--run-mode=build` ではビルドで止まり、
Arduino CLI、sketch.yaml、ESP32 board package、ライブラリ解決、公開ヘッダの最小
コンパイルを確認します。通常実行では書き込んで実行もするので、公開クラスがすべて
リンクでき、実機上で構築・設定できることまで言えます。USB device stack の実行確認では
ありません。ここでは何も列挙しません。

ビルド側の確認は `tools/build_check.py` と CI の Build Check ワークフローが
全 example × 全 profile で行っています。このモジュールにしかないのは実機側の半分、
つまりコンパイルでは証明できない「リンクして構築できる」の確認です。

## `descriptor`

USB device / configuration / HID report descriptor の byte 列を検証します。
初期仕様として、HID keyboard と HID mouse の interrupt endpoint MPS は FS / HS とも
8 bytes に固定します。keyboard + mouse composite は単一 HID interface + report ID 構成で、
report ID 付き keyboard report に合わせて endpoint MPS を 16 bytes にします。

## `audio_v2_descriptor`

新公開APIの`EspUsbAudioFunction`をS3実機上で構築し、speaker、microphone、duplexの
configuration descriptor、FS/HS packet size、mute / volume / stream state eventの
polling、stream statsのreset lifecycleを確認します。UAC1の24/32bit formatについても
subslot/bit field、packet size、transfer accountingを検証します。USB runtimeは開始しない
ため、純粋な公開API・device descriptor・control state統合テストです。

## `p4_controller_endpoints`

TinyUSBを開始せずP4上でcontroller別descriptor上限を検証します。IN endpointを5本使う
CompositeはFS controllerで拒否し、HSとP4でHSを選ぶ`Auto`では受理することを確認します。

## `fat_ramdisk`

`EspUsbDeviceMscFatRamDisk` の host 非依存ロジックを検証します。

- FAT12 boot sector の基本 field。
- volume label、FAT type、boot signature。
- 8.3 filename の正規化。
- root directory entry。
- FAT12 cluster chain。
- `exists()`、`fileSize()`、`readFile()`。
- `EspUsbDeviceMsc` への attach、read/write callback、eject callback。

## `cdc_multi`

S3 一台の CDC ACM 2 ポート構成を、通信ではなく descriptor の byte 列として検証します。
interface と association の数、各ポートが取る IN / OUT endpoint アドレス、
`EspUsbDeviceCdcSerial` の各オブジェクトが駆動する TinyUSB インスタンス番号、
`iFunction` として公開されるポート名、そしてコントローラの IN endpoint 上限を超えて
登録したときの拒否を確認します。実際の通信は `tests/peer/usb_serial_multi` が見ます。

## `composite_constraints`

`config.startTinyUsb = false` で Audio + HID / CDC / Vendor の全組み合わせを build し、
通るもの・拒否されるもの・`MAX_CLASSES` ガードが発火する位置を固定します。
composite の上限を決めるのはクラス数ではなくコントローラの非 control IN endpoint 数なので、
この計算の回帰を止めるのがここです。
