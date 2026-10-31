# EspUsbDevice FirmwareHTTP

> English: [README.md](README.md)

ホスト側に**ブラウザ**さえあれば USB 経由で firmware を更新できる例です。board は DHCP server を持つ USB ネットワークアダプター（CDC-NCM）になるので、PC はつないだ瞬間にアドレスを受け取り、device 上のページを開けます。標準の `HTTPUpdateServer` の upload フォームが、ファイルを予備の OTA partition へ直接書きます。

driver のインストールは不要（Windows、macOS、Linux はどれも NCM を標準で扱えます）、ホスト用ツールも boot mode も要りません。[ファームウェア更新ガイド](../../docs/ota-over-usb.ja.md)の経路の中で、更新する人が新しく何も用意しなくてよいのはこの方法だけです。

## ハードウェア

- ESP32-S2 / ESP32-S3 / ESP32-P4 の board。**native USB OTG** の口でつなぐ
- USB ホストとなる PC
- ログ用の別の Serial 接続

## 必要な条件

**application partition が 2 つある** partition scheme が必要です。`sketch.yaml` の全 profile が使う Arduino の「Default」scheme にはあります。1 つしかないとスケッチは `NO_OTA_PARTITION` を表示します。試す前に復旧の経路（ROM loader）を用意してください。

## 動作内容

- 「Firmware update」という名前の CDC-NCM interface を開始し、`192.168.7.0/24` の DHCP server を動かす（device は `192.168.7.1`）
- `http://192.168.7.1/` に状態のページ（実行中の image、更新先、空き、稼働時間）を出す
- `http://192.168.7.1/update` に Arduino の upload フォームを出す
- USB のネットワークが動いたら、実行中の image を確定する

## 使い方

1. スケッチを書き込み、Serial monitor を 115200 で開く。
2. native USB の口を PC へつなぐ。新しいネットワーク interface が現れ、`192.168.7.x` のアドレスを受け取る（`ip addr` / `ipconfig` で確認できる）。
3. `http://192.168.7.1/update` を開き、Firmware の欄で `.bin` を選んで upload する。terminal からなら
   `curl -F "firmware=@build/FirmwareHTTP.ino.bin" http://192.168.7.1/update`
4. ページが成功を表示し、board が新しい image で再起動する。

`.bin` は普通の Arduino の build です（「コンパイルしたバイナリをエクスポート」、または `arduino-cli compile` の `<build-path>/<sketch>.ino.bin`）。`*.merged.bin` ではありません。

## 主な API

- `EspUsbDeviceNet net(device)` — CDC-NCM Function を登録する。`net.configure(EspUsbNetConfig)` で名前と buffer の大きさを設定する（IP には既定値でよい）。
- `net.dhcpServer(true)` は `device.begin()` の前、`net.beginNetwork()` はその後にスケッチの task から呼ぶ。`net.ipConfig(local, gateway, subnet)` で既定の `192.168.7.1/24` を変えられる（[NCM ガイド](../../docs/classes/ncm.ja.md)）。
- `HTTPUpdateServer::setup(&server, "/update")` — upload フォームを追加する。Arduino の `Update` library 経由で、`EspUsbDeviceFirmwareUpdate` が使うのと同じ partition へ書く。
- `EspUsbDeviceFirmwareUpdate::targetLabel()` / `capacity()` — 更新の書込み先。状態のページに表示する。
- `EspUsbDeviceFirmwareUpdate::markValid()` — 実行中の image を確定する。

## Serial の出力例

```text
Ready at http://192.168.7.1/update
```

## 注意

- **手元から出す前に認証を付けてください。** `updater.setup(&server, "/update", user, password)` が 4 引数の形です。このままでは USB のリンクに届く人なら誰でも書き換えられます。`markValid()` も真正性の確認ではありません。
- upload した image がこのスケッチを置き換えます。NCM の更新ページを持つ build を送らないと、次の更新は [boot mode](../FirmwareBootMode/) 経由になります。
- DHCP server は gateway や DNS を通知しないので、PC のインターネットの経路は変わりません。
- `WebServer` はすべての interface で待ち受けるので、スケッチが Wi-Fi にもつなぐと同じページに Wi-Fi からも届きます。
- 1 台のホストにこの例を動かす board を 2 枚つなぐと、どちらも既定の `192.168.7.0/24` になります。`net.ipConfig(...)` で別の subnet にしてください。
- 通常の firmware の書き換えでは NVS が残ります。
- Arduino 内蔵の USB class（USB MSC / DFU / CDC on boot）をこのライブラリと同時に有効にしないでください。

## 確認の手順

更新ページを開く前に、NCM が動いてホスト側から IP で届くこと（`ping 192.168.7.1`）を確かめます。USB の列挙、IP アドレス、upload、image の検証、次の起動はそれぞれ別の段階です。`WebServer` と `HTTPUpdateServer` は USB callback ではなく `loop()` で動きます。

## v2 からの変更点

- `net.configure()` で interface に名前（「Firmware update」）を付けます。buffer の大きさはビルド時の macro ではなく、実行時の `EspUsbNetConfig` の項目です。
- `dhcpServer()` などの NCM の setter は `esp_err_t` を返します。
- 開始の失敗時に `net.startError()` も表示します。
- README に `curl` での upload を加えました。アドレス、ページ、更新の流れは変わりません。

## 関連

- [FirmwareDFU](../FirmwareDFU/)、[FirmwareMSC](../FirmwareMSC/)、[FirmwareCDC](../FirmwareCDC/)、[FirmwareVendor](../FirmwareVendor/) — 他の更新経路
- [FirmwareBootMode](../FirmwareBootMode/) — ROM loader による復旧経路
- [USB 経由のファームウェア更新](../../docs/ota-over-usb.ja.md)
- [NCM API ガイド](../../docs/classes/ncm.ja.md)
- [スケッチ](FirmwareHTTP.ino) / [全サンプル](../README.md) / [サンプルの進め方](../../docs/example-workflow.ja.md)
