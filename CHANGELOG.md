# Changelog / 変更履歴

## Unreleased
- (EN) **Breaking:** rename the USB Audio class `EspUsbDeviceAudioSink` to `EspUsbDeviceAudio`, since the one class covers both directions (speaker sink and microphone source, and both at once). The `AudioSink` / `AudioSinkM5Speaker` examples become `AudioSpeaker` / `AudioSpeakerM5`, the new microphone example is `AudioMicrophone`, and the peer tests `usb_audio` / `usb_audio_mic` become `usb_audio_speaker` / `usb_audio_microphone`. Update sketches to the `EspUsbDeviceAudio` type name; the constructor signature and methods are unchanged.
- (JA) **破壊的変更:** USB Audio クラス `EspUsbDeviceAudioSink` を `EspUsbDeviceAudio` に改名しました。1つのクラスが speaker sink と microphone source の両方向（同時も）を担うためです。example `AudioSink` / `AudioSinkM5Speaker` は `AudioSpeaker` / `AudioSpeakerM5` に、新規マイク example は `AudioMicrophone` に、peer テスト `usb_audio` / `usb_audio_mic` は `usb_audio_speaker` / `usb_audio_microphone` に改名しています。スケッチの型名を `EspUsbDeviceAudio` に更新してください（コンストラクタ引数・メソッドは不変）。
- (EN) Add an `AudioMicrophone` example and a `peer/usb_audio_microphone` test for the USB Audio source (microphone) direction (`writeMic()` / device -> host PCM): the example streams a generated 440 Hz tone as a mono 48 kHz / 16-bit recording device, and the peer test verifies the host receives non-silent PCM (S3, UAC1).
- (JA) USB Audio source（マイク）方向（`writeMic()` / device -> host PCM）の `AudioMicrophone` example と `peer/usb_audio_microphone` テストを追加しました。example は mono 48 kHz / 16-bit の録音デバイスとして 440 Hz トーンを送出し、peer テストは Host が無音でない PCM を受信することを検証します（S3, UAC1）。

## 1.1.1
- (EN) Fix a USB Audio crash where a rapid burst of volume/mute changes (e.g. dragging the Windows volume slider) rebooted the device. The audio control-transfer callback ran the user `onEvent()` on the 2048-byte Arduino USB event-loop task and overflowed its stack. Audio events now dispatch on a dedicated event loop with a generous stack, the event post is non-blocking (drops under overload instead of blocking the USB task), and the feature-unit channel index is bounds-checked. Adds a `peer/usb_audio` volume/mute flood regression test.
- (JA) USB Audio の音量/ミュートを高速連打（Windows の音量スライダードラッグ等）するとデバイスが再起動するクラッシュを修正しました。audio のコントロール転送コールバックがユーザーの `onEvent()` を 2048 バイトの Arduino USB イベントループタスク上で実行し、スタックオーバーフローしていました。audio イベントを大きめのスタックを持つ専用イベントループで配送し、ポストをノンブロッキング化（過負荷時は USB タスクを止めず破棄）、フィーチャーユニットのチャンネル番号に境界チェックを追加しました。`peer/usb_audio` に音量/ミュート連打の回帰テストを追加しています。

## 1.1.0
- (EN) **Breaking:** remove `EspUsbDeviceConfig::port` / `speed` and the `EspUsbDevicePort` / `EspUsbDeviceSpeed` enums. The device no longer selects its USB port/speed: on ESP32-P4 the Arduino core fixes the device to the high-speed (UTMI) controller and the actual link speed is negotiated with the host. Remove any `config.port` / `config.speed` assignments from sketches.
- (JA) **破壊的変更:** `EspUsbDeviceConfig::port` / `speed` と `EspUsbDevicePort` / `EspUsbDeviceSpeed` を削除しました。デバイスは USB ポート/速度を選択しません（ESP32-P4 では Arduino core がデバイスを High Speed(UTMI) コントローラに固定し、実速度はホストとのネゴで決まります）。スケッチから `config.port` / `config.speed` の代入を削除してください。
- (EN) Add the `tests/loopback/hid_keyboard_layout` test, verifying that EN_US / JA_JP symbol keys round-trip through the host on a single ESP32-P4.
- (JA) `tests/loopback/hid_keyboard_layout` を追加し、ESP32-P4 1台で EN_US / JA_JP の記号キーがホストまで往復することを検証します。
- (EN) Define USB Audio on ESP32-P4 as UAC2 / high-speed only, and remove the P4 loopback audio test: one-board loopback runs at full speed (single UTMI PHY), which cannot carry a UAC2 descriptor. Audio stays covered by the ESP32-S3 peer test (UAC1) plus manual high-speed checks.
- (JA) ESP32-P4 の USB Audio を UAC2 / High Speed 専用と定め、P4 の loopback audio テストを削除しました。1台 loopback は Full Speed（UTMI PHY は1個）で UAC2 記述子を扱えないためです。Audio は ESP32-S3 の peer テスト（UAC1）と実機 HS 手動確認でカバーします。
- (EN) Document ESP32-P4 USB port/PHY behavior and a known limitation: endpoint sizes are currently full-speed-fixed (bulk 64), so a P4 device on a real high-speed host is non-compliant for bulk (HS requires 512). The proper fix is per-speed descriptors, deferred to a real-PC / high-speed milestone. See `docs/DESIGN_NOTES.ja.md`.
- (JA) ESP32-P4 の USB ポート/PHY 挙動と既知制約を明記しました：endpoint サイズは現状 Full Speed 固定（bulk 64）で、実 High Speed ホストでは bulk が非準拠になります（HS は 512 必須）。正しい解決は per-speed descriptor で、実 PC / HS 対応マイルストーンに先送りします。詳細は `docs/DESIGN_NOTES.ja.md`。

## 1.0.0
- (EN) Initial release
- (JA) 初期リリース
