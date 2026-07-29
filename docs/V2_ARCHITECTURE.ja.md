# EspUsbDevice v2 アーキテクチャ

## 目的

v2 は Arduino-ESP32 の USB device integration を利用しない。

利用しないもの:

- `USB.begin()`
- `USB.h`
- `esp32-hal-tinyusb.h`
- `tinyusb_init()`
- `tinyusb_enable_interface()`
- `tinyusb_get_free_*_endpoint()`
- Arduino core が所有する device/configuration/string/BOS descriptor
- Arduino core が作成する TinyUSB device task

ESP-IDF の SoC/PHY/FreeRTOS API は Arduino 上でハードウェアを動かすための platform
dependency として利用する。TinyUSB はライブラリがソースと `tusb_config.h` を所有し、
Arduino core が prebuild した TinyUSB configuration に依存しない。

## Source provenance

v2のAudio実装は、現行の`EspUsbDeviceAudio.cpp`および
`EspUsbDeviceAudioDescriptors.h`を改変して作らない。両ファイルはEspressifの
USBAudioCard実装に由来するため、v1 baseline固定後にv2 branchから削除する。

新しいAudioのfirst-party sourceは、USB Audio Class仕様で定義されたwire formatと、
TinyUSBが公開するclass driver APIを入力として独立に実装する。旧ファイルから次を移植しない。

- descriptor macroおよびdescriptor byte列生成コード
- control request handler
- global Audio state
- receive task/event loop
- software volume helper

プロトコル上必須のdescriptor値、request ID、entity種別などの事実まで独自化することは
できないが、コードの構造、名前、制御フローは新設計から作る。v2 Audio完成時に旧実装との
source similarityとcopyright noticeを監査する。

third-partyとして同梱するTinyUSB自身のlicense/copyrightは変更せず、first-party sourceと
ディレクトリを分離する。

## 設計原則

### 互換性より単純なmodelを優先する

v2は既存class名やmethod配置を前提に設計しない。既存APIと同じ形が新しいownership modelにも
適合する場合だけ維持する。

- device、function、stream、serviceを別の責務として扱う
- registration、configuration、start/stopの順序を全classで統一する
- callback contextとbuffer lifetimeをAPI contractに含める
- convenience APIはcore modelの上に薄く構築し、独自状態を持たせない
- deprecated aliasや互換shimで新旧modelを同居させない

### USB controller と link speed を分離する

設定で選択するのは実リンク速度ではなく controller である。

```cpp
enum class EspUsbController {
  Auto,
  FullSpeed,
  HighSpeed,
};
```

ESP32-P4 では次の対応とする。

| controller | TinyUSB rhport | PHY 初期化 | capability |
|---|---:|---|---|
| FullSpeed | 0 | `USB_PHY_SPEED_FULL` | FS |
| HighSpeed | 1 | `USB_PHY_SPEED_HIGH` | HS/FS negotiation |

HighSpeed controller を選んでも相手が FS host なら実リンクは FS になる。descriptor callback
は request 時の negotiated speed を参照し、FS/HS descriptor table を選択する。

ESP32-S2/S3 は FullSpeed controller のみを提供する。未対応 controller の指定は暗黙に
fallback せず `ESP_ERR_NOT_SUPPORTED` とする。

### descriptor は接続前に2組作る

device の構築完了時に次を生成する。

- device descriptor
- FS configuration descriptor
- HS configuration descriptor
- device qualifier descriptor
- other-speed configuration descriptor
- string descriptor table
- 必要な場合だけ BOS / WebUSB / Microsoft OS 2.0 descriptor

TinyUSB callback は生成済みの immutable buffer を返すだけにする。endpoint address、
interface number、string index は単一の allocator が両速度の descriptor に同じ順序で
割り当てる。速度によって変えてよいのは MPS、interval、class固有の速度パラメータだけである。

### USB function と runtime driver を分離する

各 USB class は以下の責務に分ける。

1. function model: interface、endpoint、string、class固有設定を宣言する
2. descriptor writer: FS/HS descriptor を生成する
3. runtime driver: TinyUSB callback とユーザーAPIを接続する

function が core の global endpoint allocator を呼ぶ構造は禁止する。descriptor build context
から割り当て済みの interface/endpointを受け取る。

### callback はUSB taskをブロックしない

TinyUSB callback 内では次だけを行う。

- bounded buffer へのcopyまたはFIFO参照のqueue投入
- atomicなcontrol state更新
- non-blocking event通知

ユーザーcallback、音量演算、I2S write、filesystem操作、network処理はworker側で実行する。

## Runtime

`EspUsbRuntime` が次を所有する。

- `usb_phy_handle_t`
- controller/rhport
- TinyUSB init/deinit
- TinyUSB device task handle
- attach/detach state
- negotiated speed

初期化順序:

1. function登録を閉じる
2. FS/HS descriptorを検証して確定する
3. `usb_new_phy()`
4. `tusb_init(rhport, { role=device, speed=controller capability })`
5. device taskを開始する
6. pull-up/connect

終了は逆順で行い、再初期化可能な状態へ戻す。ArduinoのUSB CDC on bootと同時使用は
明示的なエラーにする。

## Audio

### UAC version と bus speed を分離する

「FS = UAC1」「HS = UAC2」という分岐は行わない。UAC version はhost互換性の選択、
bus speed はbandwidth/MPS/intervalの選択であり、別の軸である。

```cpp
enum class EspUsbAudioProtocol {
  Uac1,
  Uac2,
};
```

同じ Audio function は FS/HS の両descriptorで同じ protocol/topology を公開する。
formatが選択したcontrollerのbandwidthに収まらない場合は `begin()` を失敗させる。

### topology

旧APIの `speakerChannels` / `micChannels` を中心にしたAudio Card固定モデルを廃止する。

```cpp
EspUsbAudioFunction audio(device);
audio.protocol(EspUsbAudioProtocol::Uac2);

auto &playback = audio.addPlaybackStream();
playback.channels(2);
playback.addFormat({48000, 2, 16});

auto &capture = audio.addCaptureStream();
capture.channels(1);
capture.addFormat({48000, 2, 16});
```

Audio function は次を独立して持つ。

- 0..N playback stream (Host -> Device)
- 0..N capture stream (Device -> Host)
- clock source
- streamごとのformat/alternate setting
- 任意のmute/volume feature unit
- terminal typeとchannel map

最初の実装上限は固定長配列で定義し、heapや`std::vector`をdescriptor buildに要求しない。

### data plane

control plane と PCM data plane を分ける。

- `EspUsbAudioFunction`: descriptor、entity、class request
- `EspUsbAudioPlaybackStream`: OUT FIFO、受信queue、read/callback
- `EspUsbAudioCaptureStream`: IN FIFO、write/available
- `EspUsbAudioControl`: clock、mute、volume state

PCMの音量適用はUSB classの責務にしない。必要なら独立したDSP/helperとして提供する。
旧実装のglobal `_sample_rate`、`_spk_channels`、`_mic_channels`、`_spk_buf`、
専用event loopは廃止する。

### bandwidth validation

各alternate settingについて、worst case packet sizeをdescriptor生成前に計算する。

```text
bytes_per_second = sample_rate * channels * subslot_bytes
frames_per_second = FS ? 1000 : 8000
max_packet = ceil(bytes_per_second / frames_per_second) + clock_tolerance
```

controller、endpoint type、TinyUSB buffer上限を超えるformatはdescriptorを出さず、
具体的なエラーを返す。

## 段階的な実装順

1. 自前descriptor callbackとFS-only runtimeを実装し、S2/S3 HIDで確認する
2. CDC / MIDI / MSC / Vendor / NCMを自前descriptor builderへ移す
3. P4のFS/HS controller選択とper-speed descriptorを実装する
4. 新Audio function modelとUAC2 descriptorを実装する
5. UAC1 descriptorを追加する
6. WebUSB / Microsoft OS 2.0を必要機能として独立実装する
7. Arduino core USB integrationへのinclude/callが0件であることをCI検査する
8. 旧Audio APIと互換shimを削除する

各段階でcore経路との混在は許可しない。移行期間中に未移植classが含まれる構成は
`ESP_ERR_NOT_SUPPORTED` で失敗させ、coreへfallbackしない。

## 完了条件

- `rg 'USB\\.begin|#include [<\"]USB\\.h|esp32-hal-tinyusb|tinyusb_enable_interface|tinyusb_init\\(' src`
  がライブラリ実装について0件
- S3でFS descriptor/列挙/全class peer testが通る
- P4 rhport 0でFS device、rhport 1でHS deviceとして列挙する
- P4 HS controllerをFS hostへ接続した場合もFS descriptorを返す
- FS/HS bulk MPSがそれぞれ64/512
- Audio protocolとbus speedの全組み合わせをdescriptor unit testで検査する
- Audio callback内でuser codeを直接実行しない
- `end()` 後に別controllerで再度 `begin()` できる
