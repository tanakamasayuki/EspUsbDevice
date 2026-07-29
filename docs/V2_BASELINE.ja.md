# EspUsbDevice v2 移行前baseline

## 取得条件

- 取得日: 2026-07-29 (Asia/Tokyo)
- source commit: `8c14580`
- Arduino CLI: 1.3.1
- Arduino-ESP32: 3.3.11
- test command: `cd tests && uv run --env-file .env pytest`
- pytest: 9.1.1
- pytest-embedded: 2.8.1
- pytest-embedded-arduino-cli: 1.3.3

取得時のworktreeにはv2計画文書だけが未追跡で存在し、`src`の変更はない。

## 全suite結果

```text
113 passed in 1966.21s (0:32:46)
```

実行された範囲:

- examples 24件のS3 compile
- P4 1台loopback
- S3 host + S3 deviceのpeer test
- composite test
- Audio speaker / microphone / headset
- unit compile smoke / descriptor / composite reject / FAT RAM disk / keymap

全テストがpassした。skip、xfail、test failureはない。

## class別baseline

| class/function | S3 peer | P4 loopback | 備考 |
|---|---:|---:|---|
| HID keyboard | PASS | PASS | layout、NKROを含む |
| HID mouse | PASS | PASS | |
| HID keyboard + mouse | PASS | PASS | |
| HID custom/vendor | PASS | PASS | |
| HID consumer/system/gamepad | PASS | PASS | |
| CDC serial | PASS | PASS | |
| MIDI | PASS | PASS | |
| MSC | PASS | PASS | single-LUN |
| Vendor bulk/control | PASS | PASS | |
| NCM | PASS | 対象外 | network test 3件 |
| Audio speaker | PASS | 対象外 | S3 UAC1/FS |
| Audio microphone | PASS | 対象外 | S3 UAC1/FS |
| Audio headset | PASS | 対象外 | S3 UAC1/FS duplex |

通過したcomposite:

- CDC + MSC + Vendor
- HID + CDC
- HID + CDC + MSC
- HID + MSC
- HID + Vendor

## serial log audit

pytestは機能testを失敗させないsuspicious logを3行、既知許容logを2行検出した。

既知許容:

```text
loopback/usb_msc:
E (...) USBH: Dev 1 EP 0 STALL
Reason: GET_MAX_LUN fallback for single-LUN MSC

peer/usb_msc:
E (...) USBH: Dev 3 EP 0 STALL
Reason: GET_MAX_LUN fallback for single-LUN MSC
```

未分類だがtestはPASS:

```text
peer/composite_hid_vendor, enumerates:
E (...) USB HOST: Enqueue URB error: ESP_ERR_INVALID_STATE

peer/composite_hid_vendor, vendor works:
VENDOR_OPEN ok=1 err=ESP_ERR_INVALID_STATE
VENDOR_WRITE ok=1 err=ESP_ERR_INVALID_STATE
```

v2では機能PASSだけでなく、後者3行を出さないことを改善目標にする。

元log root:

```text
/tmp/pytest-embedded/2026-07-28_17-09-20-152178
```

`/tmp`のlogは恒久artifactではないため、上記の必要部分をこの文書へ転記した。

## descriptor unit baseline

```text
TEST_BEGIN descriptor
TEST_END pass=56 fail=0
OK
```

現在のunit testはdescriptorのbyte fieldを56項目検査する。v2のdescriptor modelでは、
旧descriptor全体との一致ではなくUSB仕様と新function modelに対する期待値へ置き換える。

## compile/size baseline

`tests/unit/compile_smoke`をS3 profileで単独compileした結果:

```text
Sketch uses 278357 bytes (21%) of program storage space. Maximum is 1310720 bytes.
Global variables use 22056 bytes (6%) of dynamic memory, leaving 305624 bytes.
Maximum is 327680 bytes.
```

これは全公開APIを参照するcompile smokeの値で、単一classの最小firmware sizeではない。
TinyUSB source同梱後の初期regression比較値として利用する。

## 現在のArduino USB integration依存

`src/EspUsbDevice.cpp`と`src/EspUsbDeviceAudio.cpp`に対し、Arduino TinyUSB integration関連の
macro/include/callを検索すると80行存在する。

主な依存:

- `esp32-hal-tinyusb.h`
- `USB.h`
- `USB.begin()`
- `tinyusb_init()`
- `tinyusb_enable_interface*()`
- `tinyusb_get_free_*_endpoint()`
- coreのdescriptor loader/string registry
- coreが作成するTinyUSB task

v2 Gate 8ではfirst-party `src`の該当参照を0件にする。

## Audio source provenance baseline

Espressif由来表示があるfirst-party file:

- `src/EspUsbDeviceAudio.cpp`
- `src/EspUsbDeviceAudioDescriptors.h`

v2では両ファイルをPhase 0.5で削除する。新Audioは両ファイルからコードを移植せず独立実装する。

## 自動suite外のbaseline

以下は今回の113件には含まれない。

- P4 High Speed controllerをPCへ接続したdescriptor dump
- P4 High SpeedでのAudio streaming
- runtime heap peak
- begin/end反復時のresource変化
- class別の最小firmware/static RAM size
- Windows WebUSB / Microsoft OS 2.0 bind

これらは該当するv2 Gateの前に旧版・新版を同じ手順で比較する。Phase 0.5開始を妨げるものは、
旧Audio sourceの挙動を必要とするP4 HS Audioだけとする。P4 HS Audioの旧実装比較が必要な場合は
v1 tagから再現する。
