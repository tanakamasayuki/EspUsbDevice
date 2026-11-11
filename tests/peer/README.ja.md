# peer テスト

> English: [README.md](README.md)

Host（公開版の EspUsbHost）と Device（EspUsbDevice）を別の board で動かし、二台の native USB 口を直結して試験します。
各 probe は、Host 側の sketch と `test_*.py`、Device 側の `peer_device/` を持ちます。

## 動く combo

| probe | combo | link |
| --- | --- | --- |
| 共通の probe（下の「共通」の表） | `s3`、`s3_s2`、`s2_s3`、`p4_p4x_hs`、`p4x_p4_hs` | FS（S3 / S2）と HS（P4 / P4X） |
| HS 専用の probe（下の「HS 専用」の表） | `p4_p4x_hs`、`p4x_p4_hs` | HS |
| `pad_link` | `s3`、`s3_s2`、`s2_s3` | FS の組だけ |

- P4 では `EspUsbController::Auto` と Host の既定 port が HS controller を選ぶので、共通の probe の sketch は同じです。
- link の MPS に依存する値は、sketch では `../../sketch_support/probe_link.h` の `PROBE_BULK_MPS`、test では `"_hs_" in request.node.name` から決めます。

```sh
uv run --locked --env-file .env pytest peer --combo s3 -q -rs
uv run --locked --env-file .env pytest peer/vendor_link --combo p4_p4x_hs -q -rs
```

## 共通

| probe | 目的 |
| --- | --- |
| `vendor_roundtrip` | Vendor の往復。同じ binary で容量と送出の方式を変えて再開始できる |
| `vendor_packet_size` | Vendor の FS bulk packet 長。USB 2.0 が許す 8 / 16 / 32 / 64 は動き、それ以外は `begin()` が失敗する |
| `vendor_soak` | 連番付きの frame を流しながら、Host を何度も再列挙させても欠落・重複・破損がない |
| `transfer_limits` | 借用転送が 16 bit を超える長さでも、DMA の分割と一度の完了で通る |
| `cancel_lifecycle` | 借用転送の取消しと、reset / `end()` との競合で、buffer が一度だけ返る |
| `direct_queue` | 借用送信を2つ出しておける。3つ目は `Busy`。2つとも順番どおりに送られ、取消し（どちらの id でも）と `end()` では2つとも一度だけ順に返る |
| `cancel_fence` | event queue の容量を超えて古い完了が残っても、新しい借用を返さない |
| `configuration_epoch` | 構成を選び直した後、queue に残った古い完了が新しい借用を返さない |
| `bulk_standard` | SET_INTERFACE と ENDPOINT_HALT が、対象の stream の借用だけを返し、他を続ける |
| `allocation_failure` | `begin()` の各確保点を順に失敗させ、停止状態へ戻って次の `begin()` が成功する |
| `function_registry` | 7 個以上の Function、登録の作り直しと巻き戻し、7 個の HID の report ID |
| `controller_capacity` | IN endpoint が多すぎる構成を、IRQ と worker を始める前に拒否し、設定を保つ |
| `endpoint_number` | 番号に空きのある endpoint で、TX FIFO を詰めて割り当てる |
| `descriptor_storage` | descriptor を数えてから書く。大きな descriptor、文字列、WebUSB、MS OS 2.0 |
| `control_core` | EP0、標準要求の検査、開始時の event 領域 |
| `reset_start` | 実際の USBRST 割込みで、ENUMDNE と worker の reset 処理より前に古い処理を無効にする |
| `suspend_resume` | 実際の bus の suspend / resume で、構成と借用を保つ。`suspended()` の値 |
| `remote_wakeup` | Device の wake 要求を Host が観測する。Host の許可、解除、reset の境界（[README](remote_wakeup/README.ja.md)） |
| `api_semantics` | 文書に書いた API の約束（HID の押下と解放、packet 長、vendor 要求の振り分け、`end()` の期限、TEST_MODE）。[TEST_PLAN](../TEST_PLAN.ja.md#公開-api-の契約を確かめる-probe) |
| `cdc_stream` | 二つの CDC と Vendor が共通 bulk で独立に動く。line coding |
| `cdc_tx` | CDC の `print` の backpressure、TX timeout、worker の中での安全、実際の完了まで待つ `flush` |
| `midi_stream` | MIDI の packet queue、cable、callback、方向ごとの mode |
| `hid_transport` | report descriptor から決まる HID の容量、複数 packet の report、control、boot mode |
| `hid_report_state` | report ID ごとの GET_REPORT の snapshot、Feature の並行更新、idle、reset |
| `hid_standard` | 複数 packet の HID report を、反対側の endpoint の PID を変えずに回復する |
| `hid_own_interface` | 専用の boot interface の Keyboard と、まとめた Mouse・Consumer Control：interface の並び、report ID、入力、LED、SET_PROTOCOL を interface ごとに確かめる |
| `hid_protocol` | 旧形式の IN を DMA に残したまま protocol を切り替え、新しい report を読む（[README](hid_protocol/README.ja.md)） |
| `hid_typed` | Gamepad、Consumer、System Control の送信と GET_REPORT（[README](hid_typed/README.ja.md)） |
| `msc_bot` | BOT の 13 case、不正な CBW、reset の手順、SCSI sense |
| `msc_storage` | MSC の staging 容量、書込みの有無、媒体の状態と error、回復。MSC と CDC の同時通信 |
| `msc_helpers` | RAM disk と FAT12 RAM disk の helper を実際の BOT で通す（[README](msc_helpers/README.ja.md)） |
| `audio_stream` | Audio の PCM buffer と ISO 転送。13 の sample rate、`end()` 後の rate 変更 |
| `audio_alternates` | PCM の形ごとの alternate を Host が選び、ISO の payload を確かめる。間を空けた ISO OUT |
| `audio_events` | 任意の event 領域、あふれ、停止中の設定、Audio の control 要求 |
| `audio_functions` | 4 つの UAC1 duplex Function が endpoint 番号を方向をまたいで共有する |
| `audio_cdc` | 周期の Audio と CDC の bulk が同じ controller と worker を共有する |
| `audio_overflow` | Playback のあふれの方針（`DropOldest` / `DropNewest`）を frame ごとに model と照合する。`test_iso_out_rate` は連続した ISO OUT の受信数を測る（合否はつけない） |
| `video_control` | UVC の control、frame の所有権、MJPEG / YUY2 の wire data。bulk の HALT |
| `video_lifecycle` | Video の format の検査と、借用 frame を返す前に DMA を止めること |
| `ncm_stream` | NCM の資源、NTB へのまとめ、Host との相互運用 |
| `ncm_ntb` | 生の NTB の検査、独立した NCM interface、alternate の回復、方向ごとの HALT |
| `ccid_stream` | CCID の容量、二つの reader、カード固定時の通知の省略。既定 `messageBytes` の値 |
| `dfu_transport` | 独立した DFU interface、開始時の資源、同期の RAM backend |

## HS 専用

| probe | 目的 |
| --- | --- |
| `hs_link` | HS 直結の列挙をその combo の向きで確かめる。Device 側の VBUS の見え方 |
| `vendor_link` | MPS 512、packet 境界の長さでの copy echo と借用転送、再開始 |
| `msc_midi` | RAM disk の MSC BOT と 2 cable の MIDI |
| `cdc_hid` | CDC ACM の echo と line coding、boot keyboard の複合 |
| `ccid` | 模擬カード。packet 境界をまたぐ ATR、APDU、Escape、slot の状態 |
| `dfu` | RAM backend の download、manifest、abort、runtime detach |
| `psram_link` | Device の PSRAM に置いた MSC 保存先と Vendor ring を HS で往復する |
| `fifo_budget` | 既定の Vendor 5 個は列挙でき、6 個目は `begin()` が `ESP_ERR_INVALID_SIZE`。Function ごとの `bulkInBuffering`：CDC 3 つと並べた Vendor だけを `Double` で 2 倍にでき、入らない `Double` は断る |

## 結線の確認

| probe | 目的 |
| --- | --- |
| `pad_link` | USB stack を使わず、USB_WRAP の test register で片側が D+/D− を駆動・pull し、反対側が読む。二台の native 口が電気的につながっているかを確かめる。新しい組を足したら、全 probe の build より先に流す |
