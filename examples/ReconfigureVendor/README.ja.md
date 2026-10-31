# EspUsbDevice ReconfigureVendor

> English: [README.md](README.md)

sketch を動かしたまま、Function の buffer の大きさを変える例です。手順は `device.end()` → `configure()` → `device.begin()` です。

v3 では容量は各 Function の実行時の設定です。`begin()` が確保し、`end()` が解放します。大きさを変えるのに再 build も reset も要りません。Host からは、抜いて挿し直したように見えます。

## ハードウェア

- native USB OTG port を持つ ESP32-S2 / S3 / P4 board
- vendor interface を開ける USB host（libusb / pyusb / WinUSB、または EspUsbHost を動かす別 ESP32）
- 別の Serial monitor 接続（書込みに使う UART / USB Serial-JTAG 側）。ログと `r` コマンドの両方に使います。

## 動作内容

- 受信・送信 ring を 128 byte（`transferBytes = 64`）にして vendor bulk function を開始し、`Started: RX=128 TX=128` を表示します。
- bulk OUT の data を bulk IN で返します。[VendorEcho](../VendorEcho/) と同じく、送りきれない残りを保持します。
- Serial monitor から `r` が届くと、`device.end()` を呼び、ring の大きさを 128 と 2048 byte で切り替え、設定し直して `begin()` を呼びます。新しい実効容量を表示します。
- `configure()` や `end()` の失敗は `esp_err_to_name()` を、`begin()` の失敗は `device.lastErrorName()` を表示します。

## 使い方

1. sketch を書き込み、Serial monitor を 115200 で開きます。
2. USB device port を Host に接続します。[VendorEcho](../VendorEcho/) の pyusb script で echo を確かめます（VID `0x303a` / PID `0x4000` は同じ）。
3. Serial monitor から `r` を送ります。`Started: RX=2048 TX=2048` が出ます。
4. Host 側では device が切断され、もう一度列挙されます。Linux では `dmesg -w` で切断と新しい device 番号が見えます。Host 側で device を開き直します。古い handle は使えません。
5. もう一度 `r` を送ると 128 byte に戻ります。

## 主要 API

- `device.end()` は USB を止め、worker と DMA の停止を待ち、`begin()` が確保したものを解放します。戻り値は `esp_err_t` です。待つのは `EspUsbDeviceConfig::stopTimeoutMs`（既定 5000 ms、0 で無期限）までです。`ESP_ERR_TIMEOUT` のときは停止要求が残り、次の `end()` で完了します。USB callback から呼ぶと `ESP_ERR_INVALID_STATE` です。
- `vendor.configure(EspUsbVendorConfig)` は停止中だけ受け付けます。動作中は `ESP_ERR_INVALID_STATE` を返します。
- `device.begin()` は ring を確保し直します。失敗したときは `device.lastErrorName()` で `esp_err_to_name()` の名前が分かります。
- `vendor.readCapacity()` / `writeCapacity()` は `begin()` が実際に確保した容量です。停止中は 0 です。
- `vendor.read()` / `write()` は VendorEcho と同じく待ちません。

## 注意

- `end()` の時点で ring に残っている data は捨てられます。sketch も保持中の chunk を消します（`length = sent = 0`）。
- Host からはケーブルを抜いたのと区別できません。Host 側のソフトは再列挙の後に device を開き直します。
- `r` コマンドは vendor interface ではなく Serial monitor で受けます。切り替えの間は vendor interface が消えるからです。
- 同じ順序は、ほかのどの Function にも、`EspUsbDeviceConfig` の identity などの変更にも使えます。

## 関連

- [VendorEcho](../VendorEcho/) - 設定変更のない同じ echo
- [USBVendor](../USBVendor/) - vendor request と WebUSB
- [Vendor API](../../docs/classes/vendor.ja.md)
- [USB device 開発ガイド](../../docs/usb-device-guide.ja.md) - configure、begin、end

## 読み方と確認の手順

### 実行前の準備

上のハードウェアの条件を確認し、board と例の profile を選びます。[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USB の役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

`start()` が開始の手順のすべてです。`configure()`、`device.begin()`、実効容量の表示の順です。`loop()` は `end()` が `ESP_OK` を返したときだけ `start()` を呼び直します。`end()` が失敗したら理由を表示し、状態はそのままです。もう一度 `r` を送ると `end()` をやり直します。

### API の使い方と改造時の注意

`end()` は切断として扱います。Host 向けの処理を先に止め、USB callback からは呼びません。`configure()` と `begin()` は別々に確かめます。失敗の理由が違うからです（値の誤りか、資源が収まらないか）。

### 一つずつ確認する

128 byte で echo を確かめ、一度切り替えて 2048 byte でも確かめます。それから切り替えを何度も繰り返します。

## v2 からの変更点

この例は v3 で新しく作りました。v2 には対応する例がありません。

- [スケッチ](ReconfigureVendor.ino)
- [全サンプル](../README.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
