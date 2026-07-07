# EspUsbDevice UsbNetwork

> English: [README.md](README.md)

ボードを USB ネットワークデバイス（CDC-NCM）にし、内蔵の DHCP サーバと小さな Web ページを
提供する例です。PC に挿すと USB ネットワークアダプタとして現れ、PC は `192.168.7.0/24` の
アドレスを受け取り、`http://192.168.7.1/` にアクセスできます。Wi-Fi 不要、ドライバインストールも
不要です（Windows / macOS / Linux は NCM を標準対応）。

「USB 設定ポータル」のパターンです。PC がデバイス上のページや API に、ネットワーク設定なしで
USB 経由で直接アクセスします。

## ハードウェア

- USB device 対応の ESP32-S3 など Arduino-ESP32 board
- PC などの USB host
- ログ確認用の Serial monitor 接続

## 動作内容

- `EspUsbDeviceNet` で CDC-NCM インターフェースを起動
- DHCP サーバを動かし、USB host に `192.168.7.x` を自動配布
- Arduino `WebServer` で `http://192.168.7.1/` に状態ページ（IP・MAC・稼働時間・閲覧回数）を配信

## 使い方

1. sketch を書き込み、通常の Serial monitor を開きます。
2. USB device port を PC につなぎます。
3. 新しいネットワークインターフェースが現れ `192.168.7.x` を取得します。
4. ブラウザで `http://192.168.7.1/` を開きます。

## 主要 API

- `EspUsbDeviceNet net(device)` は CDC-NCM function を登録します。
- `net.ipConfig(local, gateway, subnet)` はインターフェースのアドレスを設定します（既定 `192.168.7.1`）。
- `net.dhcpServer(true)` は DHCP サーバを動かします（デバイスが gateway）。代替:
  `net.dhcpClient(true)`（PC がブリッジした LAN からアドレス取得）、または両方呼ばず静的アドレス。
- `net.beginNetwork()` は lwIP/esp_netif インターフェースを起動します。`device.begin()` の後に 1 回。
- `net.localIP()` / `net.macAddress()` はアドレスと MAC を返します。
- IP スタックではなく生の Ethernet フレームを扱うなら `net.onFrame()` / `net.sendFrame()` を使い、
  `beginNetwork()` は呼びません。

## 注意

- デバイス側は CDC-NCM のみ。CDC-ECM は Arduino-ESP32 core で無効ですが、NCM は最近のホスト OS が
  標準対応しています。
- デバイスが自前 DHCP サーバを持つ構成は「PC からデバイスへ」（設定ポータル・ローカル API）に最適です。
  デバイスからインターネットへ抜けるには PC 側のブリッジ/共有が必要でホスト依存なので、その用途は
  ESP 自身の Wi-Fi を使う方が適しています。
- `WebServer` は全インターフェースで listen するため、Wi-Fi があれば同じコードがそちらでも動きます。
- 既存 Arduino USB class と同時に使う設計ではありません。

## 関連

- [CompositeHidCdcMsc](../CompositeHidCdcMsc/) - 複数の USB function を複合
- [Serial](../Serial/) - CDC ACM serial device
- `tests/manual/usb_ncm` - 最小構成の NCM + DHCP 手動/pytest テスト（host からの ping）
