# v3の開始・停止と2.xからの移行

このbranchのlibrary versionは3.0.0。開発中であり、公開release/tagを作成したという意味ではない。
検証環境はArduino-ESP32 3.3.12とS3–S3。S2/P4はcompile確認、S31は今後のSDK/port対応対象。
旧examples/testsへの互換性を合否基準にせず、`validation/`を独立した試験入口とする。

## 普通の使い方

DeviceへFunctionを登録し、必要な資源をconfigureしてbeginする。USBの自動起動は行わない。
既定のbeginはUSBを開始し、別のpoll/task呼出しは必要ない。

```cpp
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceCdcSerial usbSerial(device);

void setup() {
  EspUsbBulkStreamConfig buffers;
  buffers.rxCapacity = 256;
  buffers.txCapacity = 512;
  buffers.transferBytes = 64;
  if (usbSerial.configure(buffers) != ESP_OK) return;
  if (!device.begin()) return;
}

void loop() {
  while (usbSerial.available()) usbSerial.write(uint8_t(usbSerial.read()));
}
```

`begin()`成功は資源とUSB workerの開始を表す。Hostからの構成を待つ場合は`ready()`を使う。
`started()`は成功したbeginからendまでtrueで、USB接続待ち・reset中にも資源所有を表す。
`end()`はworker/DMAを停止し、借用bufferの返却と資源解放を待つ。戻り値はesp_err_t。
callback内のbegin/endや再設定は行わず、callbackは結果の処理や次の転送投稿に使う。

## USBを起動せずに構成を検証する

```cpp
EspUsbDeviceConfig config;
config.startMode = EspUsbDeviceStartMode::DescriptorsOnly;
if (device.begin(config)) {
  // Immutable descriptor snapshots and selected Function resources exist.
  const uint8_t *descriptor = device.configurationDescriptor(0);
  // started() == true, ready() == false; no controller/IRQ/worker is started.
  device.end();
}
// Use a fresh/default config to start normal USB afterwards.
device.begin();
```

DescriptorsOnlyでも、Functionの設定検証と選択したbufferの確保は行う。
別のbackendや実行中のUSB構成変更ではない。変更するときはendしてからconfigure/beginする。
workerを使わないため、そのmodeではstack/priority/core/event queueの設定を適用しない。
未知のstartModeはInvalidArgで拒否する。
Functionを一つも選ばないbeginもcontrollerを起動しない。空Deviceのstartedはtrue、readyはfalseとなる。

## ESP workerの設定

```cpp
EspUsbDeviceConfig config;
config.taskStackBytes = 8192;
config.taskPriority = -1; // default: highest FreeRTOS task priority
config.taskCoreId = 0;
config.eventQueueDepth = 16;
device.begin(config);
```

| 設定 | 意味 |
| --- | --- |
| taskStackBytes | byte単位。既定4096。USB worker上のapplication callbackもこのstackを使う |
| taskPriority | -1は従来と同じconfigMAX_PRIORITIES-1。明示値は0..configMAX_PRIORITIES-1 |
| taskCoreId | -1は固定しない。明示値は実行環境のcore index。single coreでは0だけ |
| eventQueueDepth | 使用するevent容量。hardware用の追加1枠は別に予約する |

[ESP-IDF FreeRTOS](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/system/freertos_idf.html)のtask stack単位に合わせる。
OS最小値未満・pointer差分で表現できないstack容量はInvalidSize、範囲外priority/coreはInvalidArg。
USB modeの検証はPHY/controller取得前に行う。実際の確保失敗はNoMemoryでrollbackする。
設定は開始中に変更せず、end後の次のbeginで適用する。既定値だけなら追加の記述は不要。

## 主な移行

| 2.xまたは移行途中のコード | v3 |
| --- | --- |
| config.startTinyUsb = false | config.startMode = EspUsbDeviceStartMode::DescriptorsOnly |
| config.startTinyUsb = true | 既定のまま、またはstartMode = EspUsbDeviceStartMode::Usb |
| device.task() | 呼出しを削除。USB workerが処理する |
| worker stack/priorityの固定値 | taskStackBytes/taskPriorityをbegin configで指定 |
| classのCFG_TUD_* / ESP_USB_DEVICE_* buffer flag | 各Functionのconfigureへ移す。旧flagはcompile error |
| class全体のstatic容量照会 | Functionごとの要求設定と開始中の実効容量を区別する |

通常のDevice/Function/beginとKeyboard/Mouse、CDC Streamの基本操作は維持する。
古いfieldや空の互換関数を残すaliasは作らない。設定の名前から実装依存を除き、意味を一つにする。
詳しい容量・copy/borrowed・callback・中断時返却は各V3_*_APIガイドを参照する。
内部のRuntime header/functionは保守実装の一部で、利用者APIではない。
