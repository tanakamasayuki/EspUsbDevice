# v3の開始・停止と2.xからの移行

このbranchのlibrary versionは3.0.0。開発中で、公開releaseやtagはまだ作っていない。

確認した環境（2026-10-31）：

| 環境 | 状況 |
| --- | --- |
| Arduino-ESP32 3.3.12、S3–S3（FS） | 主な検証環境 |
| ESP32-P4の両controller（FS loopback） | 10 classの基本通信を確認 |
| P4/P4XのHS直結（480 Mbps） | 両方向で確認 |
| P4X–Linux PC（HS） | DFU/MSCのwireを確認 |
| ESP32-S2 | compileのみ |
| ESP32-S31 | 未対応（新しいSDK/portが必要） |

旧examples/testsとの互換性は合否の基準にしない。試験の入口は独立した`validation/`。

## 普通の使い方

DeviceにFunctionを登録し、必要ならFunctionを設定して、`begin()`を呼ぶ。
USBの自動起動はしない。`begin()`がUSBを開始するので、別のpollや`task()`の呼出しは要らない。

```cpp
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceCdcSerial usbSerial(device);

void setup() {
  EspUsbBulkStreamConfig buffers;
  buffers.rxCapacity = 256;
  buffers.txCapacity = 512;
  if (usbSerial.configure(buffers) != ESP_OK) return;
  if (!device.begin()) return;
}

void loop() {
  while (usbSerial.available() && usbSerial.availableForWrite()) {
    const int value = usbSerial.read();
    if (value >= 0) usbSerial.write(uint8_t(value));
  }
  delay(1);
}
```

`EspUsbBulkStreamConfig`の既定は`rxCapacity = 512`、`txCapacity = 512`、`transferBytes = 512`。

### 状態の照会

| 関数 | trueになる条件 |
| --- | --- |
| `started()` | `begin()`が成功してから`end()`まで。資源を所有していることを表す。接続待ちやreset中もtrue |
| `ready()` | Hostが構成（SET_CONFIGURATION）を済ませている |
| `suspended()` | busが休止している。休止中もHostの構成は保たれるので`ready()`はtrueのまま |

- `suspended()`は、USB開始前、`DescriptorsOnly`、`end()`後はfalse。
- 休止はresetや停止ではない。借用bufferは、完了・取消し・reset・`end()`のどれかで返るまで保持される。
- 送信できるか、完了したかは、Functionの転送APIの戻り値と完了通知で判断する。
  `ready() && !suspended()`は、次の転送の成功を約束しない。

### 終了

- `end()`はworkerとDMAを止め、借用bufferの返却と資源の解放まで待つ。戻り値は`esp_err_t`。
- USB worker上のcallbackから`end()`を呼ぶと`ESP_ERR_INVALID_STATE`。
- callbackの中では`begin()`/`end()`や再設定をしない。callbackは結果の処理と次の転送の投稿に使う。

## USBを起動せずに構成を確かめる

```cpp
EspUsbDeviceConfig config;
config.startMode = EspUsbDeviceStartMode::DescriptorsOnly;
if (device.begin(config)) {
  // descriptorと、選んだFunctionの資源ができている。
  const uint8_t *descriptor = device.configurationDescriptor(0);
  // started() == true、ready() == false。controller、IRQ、workerは動いていない。
  device.end();
}
device.begin();  // 既定の設定（startMode = Usb）で通常どおり開始する
```

- `DescriptorsOnly`でも、Functionの設定の検証と、選んだbufferの確保は行う。
- 別のbackendではない。設定を変えるときは`end()`してから`configure()`/`begin()`する。
- workerを使わないので、stack/priority/core/event queueの設定は使わない。
- 未知の`startMode`は`ESP_ERR_INVALID_ARG`で拒否する。
- Functionを一つも登録しない`begin()`もcontrollerを起動しない。このとき`started()`はtrue、`ready()`はfalse。

## workerとcontrollerの設定

```cpp
EspUsbDeviceConfig config;
config.taskStackBytes = 8192;
config.taskPriority = -1;  // 既定：FreeRTOSの最高priority
config.taskCoreId = 0;
config.eventQueueDepth = 16;
device.begin(config);
```

| 設定 | 既定 | 意味と制約 |
| --- | --- | --- |
| `taskStackBytes` | 4096 | byte単位。USB worker上の利用者callbackもこのstackを使う。OSの最小値未満は`ESP_ERR_INVALID_SIZE` |
| `taskPriority` | -1 | -1は`configMAX_PRIORITIES - 1`。明示するなら0〜`configMAX_PRIORITIES - 1`。範囲外は`ESP_ERR_INVALID_ARG` |
| `taskCoreId` | -1 | -1はcoreを固定しない。明示するなら実在するcore番号（single coreでは0だけ）。範囲外は`ESP_ERR_INVALID_ARG` |
| `eventQueueDepth` | 16 | 割込みeventを保持する数。0は不可。hardware用の予備1件は別に確保する |
| `controller` | `Auto` | `Auto`はHS portがあればHS（P4）、なければFS（S2/S3）。`FullSpeed`/`HighSpeed`で明示もできる |
| `vbusSensing` | false | trueはboardのVBUS検出信号を使う。検出回路と配線があるboardだけ |

- stackの単位は[ESP-IDF FreeRTOS](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/system/freertos_idf.html)に合わせてbyte。
- 設定の検証は、PHYやcontrollerを取る前に行う。確保の失敗は`ESP_ERR_NO_MEM`で、取得済みの資源は戻す。
- 設定は開始中には変えられない。`end()`の後、次の`begin()`で反映する。既定値のままなら何も書かなくてよい。

## HSで動かすときの注意（P4）

P4の既定（`controller = Auto`）はHS controllerを使う。HSではbulkの最大packet（MPS）が64から512になる。
2026-10-31にP4/P4XのHS直結で確認した動作は次のとおり。

| 対象 | HSでの動作 |
| --- | --- |
| CDC/Vendor/MIDIの`transferBytes` | MPS未満でも受け付ける |
| MSCの`transferBytes`（既定512） | MPSの倍数が必要。64や256は`begin()`が`ESP_ERR_INVALID_SIZE` |
| CCIDの`messageBytes` | 既定0（自動）は512になる。明示値が512未満なら`begin()`が`ESP_ERR_INVALID_SIZE` |
| 借用RX（`readDirect`）の容量 | MPSの倍数（512の倍数）が必要。FSでは64の倍数でよかった |
| HIDのinterrupt endpoint | 1024 byteまで使える |
| 非controlのIN endpoint | 7本まで（S3とP4 FSは4本） |

FSで動いていた設定でも、HSでは`begin()`が失敗する場合がある。`begin()`の戻り値と`lastError()`を確認する。

## 主な移行

| 2.xまたは開発途中のコード | v3 |
| --- | --- |
| `config.startTinyUsb = false` | `config.startMode = EspUsbDeviceStartMode::DescriptorsOnly` |
| `config.startTinyUsb = true` | 既定のまま、または`startMode = EspUsbDeviceStartMode::Usb` |
| `device.task()` | 呼出しを削除する。USB workerが処理する |
| workerのstack/priorityの固定値 | `taskStackBytes`/`taskPriority`を`begin()`の設定で指定する |
| classの`CFG_TUD_*`、`ESP_USB_DEVICE_*`のbuffer flag | 各Functionの`configure()`へ移す。旧flagはcompile error |
| class全体のstaticな容量照会 | Functionごとの要求設定と、開始中の実効容量を区別する |
| `CFG_TUD_VBUS_DETECT_HW` | `EspUsbDeviceConfig::vbusSensing` |

- 通常のDevice/Function/`begin()`、Keyboard/Mouse、CDCの基本操作は変わらない。
- CDCはArduino `Stream`を直接実装し、`peek`/`readBytes`/`setTimeout`などが使える。
- 共通bulk転送層とMIDIは`Print`ではない。`Print&`へ渡すなら具体的なCDC/Vendor objectを渡す。MIDIはpacket APIを使う。
- 古いfieldや空の互換関数を残すaliasは作らない。
- 容量、copy/borrowed、callback、中断時の返却の詳細は、各`V3_*_API`ガイドを参照する。
- 内部のRuntime header/関数は保守実装の一部で、利用者APIではない。

## 借用転送の上限と確保量

`writeDirect`/`readDirect`は、65535 byteの内部制約を外した。もっと大きな領域も一度に投稿できる。

- `maxDirectTransfer()`/`maxDirectReceive()`は表現できる上限で、空きheapや確保できる量ではない。
- 上限値をそのままmallocしていたコードは、アプリ側でbufferの予算を決める形に変える。
- 投稿が拒否されたら、bufferは呼出し側のまま。受理されたら、完了callbackまで保持する。
- 内部の分割に合わせて手で分けたり、途中の完了に対応したりする必要はない。

[Vendorの詳細と費用](V3_VENDOR_API.ja.md#65535-byteを跨ぐ借用転送)を参照。

## Audioの外部buffer

開発途中の外部ringの指定に、byte長を足した。

- `playbackBuffer`と一緒に`playbackBufferBytes`、`captureBuffer`と一緒に`captureBufferBytes`を渡す（`sizeof(配列)`など）。
- 内部で確保する基本の使い方（pointer=nullptr、byte長=0）は変更不要。
- byte長の省略を旧動作に読み替える互換処理はない。formatを足して必要量が増えても、不足を検出するため。
- 容量不足は`begin()`が`ESP_ERR_INVALID_SIZE`。pointerとbyte長の組の誤りは`configure()`が`ESP_ERR_INVALID_ARG`。

## System Controlの数値

System ControlはConsumer Controlと同じく、標準のusage IDを引数にする。

- 名前付きの`POWER_OFF`/`STANDBY`/`WAKE_HOST`の呼出しはそのまま使える。値は0x81/0x82/0x83になった。
- 整数1/2/3の直書きや、`usage()`との比較は変更が必要。0による解放は変わらない。
- 対応しない値は送信せずに拒否する。

[詳細と利点・影響](V3_HID_API.ja.md#system-controlのusage指定)を参照。

## CH32RVとの調整によるv3開発途中の変更（2026-10-30）

### Videoの投稿API

詳しい投稿APIは、idの出力引数をやめて、共通の`EspUsbTransferSubmission`を返す形にした。

```cpp
const auto submission = camera.submitFrame(frame, length);
if (submission) {
  // frameはonFrameResultで返るまで、有効で変更しないまま保つ。
  // 取り消すならcamera.cancelFrame(submission.id);
} else {
  // submission.errorを確認する。idは0で、frameは借用されていない。
}
```

単純な`camera.sendFrame(frame, length)`は従来どおりboolを返す。
[Videoの拒否理由と所有権](V3_VIDEO_API.ja.md#frameの所有権と通知)を参照。

### CDCの送信待ち

- 通常のtaskでの`write`/`print`は、空きが足りなければTX timeoutまで待つ。既定は1000 ms。
- `flush()`も、送信中のDMAと必要な終端ZLPまで期限付きで待つ。
- 以前の動作（すぐpartialで戻る、flushは送出の開始だけ）が必要なら`console.setTxTimeoutMs(0)`。
- 読出しの`Stream::setTimeout`とは独立。DTRの必須化や、共通bulk/Vendor/MIDIへの待ちの追加はしない。
- USB workerのcallbackからは待たない。ISRからの送信は受け付けない。
- 宣言、`begin()`、`print`の基本例は変更不要。

[CDCの期限・停止とflush](V3_CDC_API.ja.md#cdc送信の期限とflush)を参照。

### Audio playbackのあふれ

ringがあふれたときの既定を、`DropOldest`（古い未読を捨て、最新の完全なframeを残す）に変えた。
以前と同じく未読を残し、入らない新着を捨てたい場合は`DropNewest`を明示する。

```cpp
EspUsbAudioConfig config;
config.playbackOverflowPolicy = EspUsbAudioOverflowPolicy::DropNewest;  // 以前の動作
audio.configure(config);
```

- `configure()`→`begin()`の手順と、あふれない場合の動作は変わらない。
- `overrunBytes`には、新着の破棄に加え、`DropOldest`で追い出した過去のPCMも数える。
- Captureのpartial writeは変わらない。

[保持方針と統計](V3_AUDIO_API.ja.md#playbackのあふれ方針playbackoverflowpolicy)を参照。

### DFUのDETACH

DFU Runtimeの`onDetach`は、SETUPの時点で受付可否を返すbool callbackになった。
従来のvoid `onDetach`で行っていた移行処理（既定の再起動を置き換えるもの）は、`setDetachHandler`へ移す。

```cpp
runtime.onDetach([]() { return !busy; });             // 拒否が必要なときだけ。falseでSTALL
runtime.setDetachHandler([]() { startMyUpdate(); });  // 受付後の移行を自分で行うとき
```

どちらも登録しない単純な使い方は変更不要。
[DETACHの受付と移行](V3_DFU_API.ja.md#detachの受付と移行2026-10-30-実装)を参照。

### MSCの媒体変更

- `mediaPresent(true)`：Hostが構成済みなら、次のcommandを一度だけUNIT ATTENTION（06/28/00）で失敗させる。
- `mediaPresent(false)`：進行中のREAD/WRITEの残りを02/3A/00で失敗させ、そのcommandの以後のcallbackを呼ばない。
- 呼び方は変わらない。再挿入の直後にTUR成功を前提にしていたHost側の処理は、一度の06/28を許容する。

[媒体変更とwire確認](V3_MSC_API.ja.md#espでの実装とwire確認2026-10-30)を参照。

## CCIDの既定容量（2026-10-31）

`EspUsbCcidConfig::messageBytes`の既定を320から0（自動）に変えた。
自動の値は`begin()`のときに`max(271, Bulk-OUT MPS)`に決まる（FSでは271、HSでは512）。

- 理由：既定320はHSのMPS 512より小さく、P4のHSでは`begin()`が必ず失敗していた。
- 影響：FSでは既定の容量が320から271に減る。271は短いAPDU一つ分。
  それより大きい容量が必要なら明示する。明示値がMPS未満なら`begin()`が`ESP_ERR_INVALID_SIZE`。
- 決まった値は`messageCapacity()`とdescriptorの`dwMaxCCIDMessageLength`に出る。

[CCIDガイド](V3_CCID_API.ja.md)と[API判断](V3_API_DECISIONS.ja.md#ccidの既定messagebytes2026-10-31)を参照。
