# Typed HID の送信

Gamepad、Consumer Control、System Control の送信 API が、descriptor どおりの意味の report を出すことを確かめます。
S3 の FS（combo `s3`）と、P4 / P4X の HS 直結（`p4_p4x_hs`、`p4x_p4_hs`）で動きます。

## 目的

- 各 API が出す raw input と GET_REPORT の値が、既知の byte 列と一致すること。
- Host が EP0 で読んだ report descriptor を解析し、値の意味（usage、単位、範囲）が正しいこと。
- 不正な値の指定を拒否し、余分な input を出さないこと。

## 手順

- Gamepad、Consumer、System のそれぞれ単体と、3 種を一つの HID にまとめた構成で、各 2 回開始します。
- Gamepad：符号付き 6 軸、hat、32 bit の button、構造体と引数の両方の形式、`releaseAll`。
  hat は上 / 右 / 左上の角度と、中央が Null であること（variable field の Null）を確かめます。
- Consumer / System：`press`、`sendUsage`、`release`、`click`、`usage()`。`click` は押下と解放の 2 report を出します。
- System Control は標準 usage 0x81 / 0x82 / 0x83 で指定し、wire 上の selector 1 / 2 / 3 と、解放の 0 を照合します。
  0 は array の「押していない」として確かめます（HID 1.11 の array の規定）。
- 複合の構成では、単位と物理範囲を持つ独自の HID を先に登録し、その global 項目が Gamepad に漏れないことを確かめます。
- 不正な値の API 呼出しを拒否し、`usage()` と GET_REPORT が変わらず、Host の監視中に余分な input がないことを確かめます。

## 範囲外

- Host は S3 / P4 上の EspUsbHost です。PC の OS のシステム操作やゲームとの互換性は確かめません。

参考：[HID 1.11](https://www.usb.org/sites/default/files/hid1_11.pdf)、[HID Usage Tables](https://usb.org/document-library/hid-usage-tables-15)
