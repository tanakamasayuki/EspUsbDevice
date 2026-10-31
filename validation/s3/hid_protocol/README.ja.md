# 未完の HID IN と protocol の切替

boot / report protocol を切り替えるとき、まだ Host が読んでいない旧形式の IN report を Host に渡さないことを確かめます。
S3 の FS（combo `s3`）と、P4 / P4X の HS 直結（`p4_p4x_hs`、`p4x_p4_hs`）で動きます。

## 目的

- Device の DMA に旧形式の report が残ったまま SET_PROTOCOL が来ても、切替後に Host が最初に読む report が新形式であること。
- 現在の形式と長さの違う送信を `InvalidSize` で拒否すること。

## 手順

- Host はこの probe だけ ESP-IDF の公開 USB Host API で interface を取ります。
  通常の EspUsbHost は HID IN を自動で poll するので、旧形式の report を Device の DMA に確実に残す条件を作れないためです。
- Host は protocol を切り替えるまで interrupt IN を一度も投稿しません。Device の二回目の非 blocking 送信が拒否されることで、最初の report が未完であることを確かめます。
- Keyboard（NKRO 29 byte / boot 8 byte）と Mouse（report 4 byte / boot 3 byte）で、それぞれ 4 回切り替えます。
- 切替後に初めて IN を投稿し、30 ms のあいだ完了しないこと（旧 report が届かないこと）を確かめます。
- その IN へ新しい marker の report を送り、長さと全 byte を照合します。
- Core の `CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK` が有効なので、対象の VID/PID を許可する `enum_filter_cb` を登録します。null では列挙が中止されます。
- 終了時は transfer を返し、interface / device / client と Host を解放します。返っていない DMA 領域は free しません。

## 範囲外

- core の event queue の満杯や、人工的な古い完了 event の注入
- IN 転送の途中の bus reset / `end()`

参考：[HID 1.11](https://www.usb.org/sites/default/files/hid1_11.pdf) §7.2.6（Set_Protocol）、[ESP-IDF USB Host](https://docs.espressif.com/projects/esp-usb/en/latest/esp32s3/usb_host.html)
