# v3の開始・停止と2.xからの移行

このbranchのlibrary versionは3.0.0。開発中であり、公開release/tagを作成したという意味ではない。
検証環境はArduino-ESP32 3.3.12とS3–S3。P4は両controllerのFS loopbackでVendor/CDC/HIDとborrowed DMAを確認。
S2はcompile確認、P4の480 Mbps HS linkとS31の新SDK/portは今後の対象。
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
  while (usbSerial.available() && usbSerial.writeAvailable()) {
    const int value = usbSerial.read();
    if (value >= 0) usbSerial.write(uint8_t(value));
  }
  delay(1);
}
```

`begin()`成功は資源とUSB workerの開始を表す。Hostからの構成を待つ場合は`ready()`を使う。
`started()`は成功したbeginからendまでtrueで、USB接続待ち・reset中にも資源所有を表す。
`suspended()`は実際のbus休止状態を示す。Hostの構成は休止中も維持するためready()はtrueのまま。
USB開始前、DescriptorsOnly、end後はfalse。送信受付・完了の可否はFunctionの転送APIで判断する。
休止はresetや停止ではなく、借用bufferは完了・取消し・reset・endによる返却まで保持する。
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
CDCはArduino Streamを直接実装し、peek/readBytes/setTimeout等を使える。
共通bulk転送層とMIDIはPrintではない。Print&へ渡す場合は具体的なCDC/Vendor objectを渡し、
MIDIではpacket APIへ移す。Streamの読出し待ちとUSB workerの設定は別のものとして扱う。
古いfieldや空の互換関数を残すaliasは作らない。設定の名前から実装依存を除き、意味を一つにする。
詳しい容量・copy/borrowed・callback・中断時返却は各V3_*_APIガイドを参照する。
内部のRuntime header/functionは保守実装の一部で、利用者APIではない。


## borrowed転送の上限と確保量

writeDirect/readDirectは65535 byteの内部制約を外し、より大きな所有領域も投稿できる。
上限queryはlogicalな表現上限で、空きheapや確保可能量ではない。
上限値をそのままmallocする旧コードは、applicationのbuffer予算を別に決める形へ変更する。
buffer容量以下・query上限以下で投稿し、拒否ならcallerが所有を保持、受理ならcallbackまで保持する。
分割単位に合わせた利用側の手動分割や、途中の完了通知への対応は不要。
[Vendorの詳細と費用](V3_VENDOR_API.ja.md#65535-byteを跨ぐ借用転送)を参照。


## Audioの外部buffer

v3開発途中の外部ring指定には提供長を追加した。
playbackBufferと同時にplaybackBufferBytes、captureBufferと同時にcaptureBufferBytesへsizeof配列等を渡す。
内部確保（pointer=nullptr、byte長=0）の基本利用は変更不要。
形式追加で必要量が増えても不足を検出できるよう、byte長省略を旧動作へ読み替える互換処理は入れない。
容量不足のbeginはESP_ERR_INVALID_SIZE、pointerとbyte長の組の誤りはconfigureがESP_ERR_INVALID_ARG。
configureは戻り値、beginはboolとDevice.lastErrorを確認する。

## System Controlの数値

System ControlはConsumer Controlと同じく標準usage IDを引数にする。
名前付きPOWER_OFF/STANDBY/WAKE_HOSTの呼出しは維持し、値は0x81/0x82/0x83へ揃える。
整数1/2/3の直書きやusage()との比較は変更が必要。0による解放は維持する。
wire selectorとusageを正しくdescriptorで対応させ、未対応値は送信せず拒否する。
[詳細と利点・影響](V3_HID_API.ja.md#system-controlのusage指定)を参照。
