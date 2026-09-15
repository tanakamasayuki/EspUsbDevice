# EspUsbDevice FirmwareHTTP

> English: [README.md](README.md)

host 側に**ブラウザ以外なにも要らない** USB 経由のファームウェア更新です。ボードが
DHCP サーバ付きの USB ネットワークアダプタ（CDC-NCM）になるので、挿した瞬間に PC は
アドレスを受け取り、デバイス上のページを開けます。Arduino 標準の
`HTTPUpdateServer` のアップロードフォームが、そのまま空いている OTA partition へ
書き込みます。

ドライバ不要（Windows / macOS / Linux は NCM を標準対応）、host tool 不要、
boot mode 不要。[ファームウェア更新ガイド](../../docs/ota-over-usb.ja.md) の各経路の
中で、更新する人が新しく用意するものが何も無い唯一の経路です。

## ハードウェア

- USB device 対応の ESP32-S2 / ESP32-S3 / ESP32-P4 board
- USB host となる PC
- ログ確認用の Serial 接続

## 前提

**application partition が 2 つある** partition scheme が必要です。Arduino の
「Default」にはあります。1 つしか無い場合は `NO_OTA_PARTITION` を表示します。

## 動作内容

- `192.168.7.0/24` の DHCP サーバ付き CDC-NCM interface を起動
- `http://192.168.7.1/` に状態ページを配信
- `http://192.168.7.1/update` に Arduino のアップロードフォームを配信
- USB リンクが上がったら動作中のイメージを確定

## 使い方

1. スケッチを書き込み、Serial monitor を開きます。
2. USB device port を PC に接続します。新しいネットワークインターフェースが現れ、
   `192.168.7.x` のアドレスを受け取ります。
3. `http://192.168.7.1/update` を開き、`.bin` を選んでアップロードします。
4. ボードがそのイメージで再起動します。

`.bin` は Arduino の素のビルド（スケッチ → コンパイル済みバイナリをエクスポート、
または `arduino-cli compile` が残す `build/`）です。

## 主な API

- `EspUsbDeviceNet net(device)` + `net.dhcpServer(true)` + `net.beginNetwork()`
  が USB ネットワークデバイス本体です。アドレス周りの選択肢は
  [`UsbNetwork`](../UsbNetwork/) を参照してください。
- `HTTPUpdateServer::setup(&server, "/update")` がアップロードフォームを追加します。
  書き込みは Arduino の `Update` ライブラリ経由で、`EspUsbDeviceFirmwareUpdate`
  と同じ partition に入ります。
- `EspUsbDeviceFirmwareUpdate::targetLabel()` / `capacity()` — 更新先の情報。
  状態ページに表示しています。
- `EspUsbDeviceFirmwareUpdate::markValid()` — 動作中のイメージを確定します。

## 注意

- **机の外に出す前に認証を付けてください。** `setup(&server, "/update", user,
  password)` の 4 引数版があります。このままでは USB リンクに届く相手なら誰でも
  書き換えられます。
- アップロードしたイメージがこのスケッチを置き換えます。NCM 更新ページを持つ
  ビルドを送ってください。そうしないと次の更新は
  [boot mode](../FirmwareBootMode/) 経由になります。
- `WebServer` は全インターフェースで待ち受けるので、Wi-Fi にも繋ぐスケッチなら
  同じページに Wi-Fi 経由でも到達できます。
- 同じ host に 2 台繋ぐと既定ではどちらも `192.168.7.0/24` になります。
  `net.ipConfig(...)` で別のサブネットにしてください。
- このライブラリは Arduino 標準の USB device class と同時には使えません。
