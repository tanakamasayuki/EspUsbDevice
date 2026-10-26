# 未完HID INのprotocol切替試験

S3–S3の通常comboを使い、pytestのdut/peers以外のfixture/hookを追加しない。
既存examples/testsから独立している。

HostはこのcaseだけESP-IDFの公開USB Host APIでinterfaceを取得する。
通常のEspUsbHostはHID INを自動pollするため、旧形式を確実にDevice DMAへ保留する条件を作れない。
manual Hostはprotocol切替までinterrupt INを一度も投稿せず、二つ目の非blocking送信が拒否されることで
最初のreportが未完了であることを確認する。

Keyboard(NKRO29/boot8 byte)とMouse(report4/boot3 byte)で各4回切り替える。
切替後に初めてINを投稿して30 ms未完了であることを確認し、旧reportが届かないことを調べる。
そのINへ新しいmarkerのreportを送って、正しい長さと全byteを照合する。
現在の形式と異なる長さの送信もInvalidSizeで拒否されることを確認する。

このCoreではCONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACKが有効なので、対象VID/PIDを許可する
標準enum_filter_cbを登録する。nullでは列挙が中止される。
開始/取得の診断値をserialへ残し、終了時はtransferの返却、interface/device/clientとHostを解放する。
未返却DMA領域はfreeしない。試験失敗や停止失敗はskipせずfailedとする。

このcaseは実hardware停止の確認であり、core event queue満杯/人工的な古い完了eventの注入や、
IN転送途中のbus reset/endを網羅する試験ではない。
参考: [HID 1.11](https://www.usb.org/sites/default/files/hid1_11.pdf)、
[ESP-IDF USB Host](https://docs.espressif.com/projects/esp-usb/en/latest/esp32s3/usb_host.html)。
