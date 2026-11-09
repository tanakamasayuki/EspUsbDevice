# テスト計画

> English: [TEST_PLAN.md](TEST_PLAN.md)

実機試験で確かめた範囲、未確認の範囲、試験の期待値を決めている前提を書きます。
実行方法は [README.ja.md](README.ja.md)、残作業は [TODO](../TODO.ja.md) にあります。

## 確認済みの範囲

| 範囲 | combo | 確認したこと |
| --- | --- | --- |
| S3 ↔ S3（FS） | `s3` | `peer/` の全 probe。10 class、lifecycle、確保失敗からの回復、取消し・reset・end の競合、標準要求、suspend / resume、remote wakeup |
| S2 ↔ S3（FS、両方向） | `s3_s2`、`s2_s3` | `peer/` の全 probe。S2 が Device のときと Host のときの両方 |
| 旧 P4 loopback（FS、両 controller） | `p4`、`p4_psram` | 10 class の基本通信、65537 byte までの借用 DMA、再開始。PSRAM 上の MSC 保存先と Vendor ring |
| 旧 P4 ↔ P4X の HS 直結（両方向） | `p4_p4x_hs`、`p4x_p4_hs` | `peer/` の全 probe と HS 専用 probe。HS の列挙、MPS 512、借用転送、Audio / Video の ISO、PSRAM 上の保存先 |
| Linux PC → P4X（HS） | `p4x_pc` | MSC の媒体交換（06/28/00、02/3A/00、residue）、FAT RAM disk の `fsck.fat`、DFU DETACH |
| Linux PC → S2（FS） | `primary=s2_pc` | MSC の媒体交換、DFU DETACH。常設の組ではない |
| build | — | S2 / S3 / P4 / P4X の compile。未使用 class がリンクされないこと |

## 未確認の範囲

| 範囲 | 状態 |
| --- | --- |
| ESP32-S2 の ROM DFU への移行 | `rebootToRomDfu()` は未確認 |
| ESP32-S31 | 未対応 |
| Windows / macOS | class の bind、各 class の動作とも未確認 |
| Linux PC の class driver | MSC / DFU の wire 検査だけ。CDC / HID / Audio / Video / NCM の OS driver との組合せは未確認 |
| SD card と OTA の実媒体 | RAM backend の試験だけ |
| 長時間の連続運転 | 回数を決めた反復（再接続を含む）まで |
| 実ケーブルの抜去による VBUS 検出（`vbusSensing=true`） | 未確認 |
| USB DMA が PSRAM を直接読み書きする構成 | 製品が拒否するので対象外 |
| Arduino-ESP32 4 系 | 未確認。基準は 3.3.12 |
| 任意の競合順序（SETUP / reset / end / SET_CONFIGURATION の全組合せ） | 代表的な順序だけ |

## 公開 API の契約を確かめる probe

文書に書いた API の約束は、次の probe で wire の上から確かめます。

| probe | 確かめること |
| --- | --- |
| `peer/api_semantics` | Keyboard の press の足し込み、修飾キーの bit、release 後の詰め、tap が足した分だけ離すこと、`releaseKey()`、7 つ目のキーの拒否。Mouse の `move()` が button を保つことと範囲外の拒否。HID の packet 長（`inputPacketSize()`、`EspUsbDeviceHidCustom` の `inputReportSize` 200 が FS 64 / HS 200）。WebUSB / MS OS 2.0 と同じ bRequest のほかの vendor 要求が sketch に届くこと。callback が戻らないときの `end()` の `ESP_ERR_TIMEOUT` と再呼出し。TEST_MODE が HS だけで受け付けられること |
| `peer/vendor_packet_size` | Vendor の FS bulk 長 8 / 16 / 32 / 64 が動き、それ以外を `begin()` が拒否すること。HS は常に 512 |
| `peer/fifo_budget` | HS で既定の Vendor 5 個は列挙でき、6 個目は `begin()` が `ESP_ERR_INVALID_SIZE` |
| `pc/msc_fat` | `EspUsbDeviceMscFatRamDisk` の 1 / 4 / 24 MiB の volume を PC が読み、`fsck.fat` で検査する |

## HS で変わる値

HS 対応の controller では、bulk の MPS が 64 から 512 になります。試験の期待値も変わります。

| 項目 | FS | HS |
| --- | --- | --- |
| bulk MPS | 64 | 512 |
| MSC `transferBytes` | 64 の倍数 | 512 の倍数（それ以外は `begin()` が `ESP_ERR_INVALID_SIZE`） |
| CCID の既定 `messageBytes` | 271 | 512 |
| 借用受信の容量 | 64 の倍数 | 512 の倍数 |
| 非 control IN endpoint の上限 | 4 | 7 |
| HID interrupt endpoint | 64 byte まで | 1024 byte まで |
| UAC2 feedback | 10.14 を 3 byte | 16.16 を 4 byte |

- HS 対応の controller を選ぶと、link が FS でも bulk の容量を 512 で検査します。P4 の loopback で MSC の staging 64 が拒否されるのはこのためです。
- CDC / Vendor / MIDI の `transferBytes` は MPS 未満でも受け付けます。
- `begin()` は構成全体の IN endpoint が TX FIFO に入るかを検査し、入らなければ `ESP_ERR_INVALID_SIZE` です。P4 の HS controller では、既定の Vendor は 5 個まで入り、6 個目で失敗します。
- link の速度は、Device 側では `device.speed()` で読めます。

Host 側（ESP-IDF の USB Host）の制約も試験の組み方に影響します。

- bulk IN の `num_bytes` は MPS の倍数が必要です。
- `usb_host_endpoint_clear` は Host 側の data toggle を戻しません。HALT の試験は、clear 前の packet 数が偶数になるように組みます。
- HS の HID では、Host の周期 OUT FIFO の既定（512 byte）が足りない場合があります。
- Vendor IN は、公開 EspUsbHost の cache 準備済み read queue と callback で受けます。同期の `vendorReadSync()` は P4 で cache の問題があるため使いません。

## P4 と P4X の build 設定

製品の source に P4 と P4X を区別する処理はありません。違いは build 設定と SDK です。

| 項目 | 旧 P4（revision v1.x） | P4X（revision v3.x） |
| --- | --- | --- |
| FQBN | `esp32:esp32:esp32p4:ChipVariant=prev3,USBMode=default,CDCOnBoot=default` | `esp32:esp32:esp32p4:ChipVariant=postv3,USBMode=hwcdc,CDCOnBoot=cdc` |
| `Serial` の経路 | UART0 → USB-UART | USB Serial/JTAG（HWCDC） |
| library package | `esp32p4_es-libs` | `esp32p4-libs` |
| SDK が対応する revision | v1.x | v3.1 以上 |
| CPU clock（build 設定） | 360 MHz | 400 MHz |
| HS non-periodic / periodic TX queue depth | 4 / 4 | 8 / 16 |
| HS endpoint 数 / IN endpoint 数 / FIFO | 15 / 8 / 1024 word | 同じ |

- bench の P4X は log を USB Serial/JTAG で出すので、試験の profile は `USBMode=hwcdc` です。`USBMode=default` だと `Serial` が UART0 になり、制御用の port に log が出ません。試験対象は HS controller で、USB Serial/JTAG とは別の端子です。
- examples の `esp32p4x` profile は `USBMode=default` です。試験の profile とは別です。
- P4X の SDK が対応するのは v3.1 以上です。v3.0 の実機は対応済みとして扱いません。
- endpoint 数と FIFO 容量は同じですが、旧 P4 の成功を P4X の合格にはせず、両方で試験します。

## PSRAM

- P4 系の bench board はすべて 32 MiB の PSRAM を持ちます（`single/psram` で実測）。
- PSRAM を使う試験の profile は FQBN に `PSRAM=enabled` を足します（`p4_psram`、`peer/psram_link` の Device 側）。それ以外の P4 profile は PSRAM を有効にしません。
- 製品は、ring、MSC の保存先など CPU が読み書きする領域に PSRAM を使えます。USB の DMA が触る staging は内部 RAM のままです。
- 借用転送（`readDirect` / `writeDirect`）に PSRAM の buffer を渡すと `NotDmaCapable` で拒否します。試験もこの前提で書きます。
