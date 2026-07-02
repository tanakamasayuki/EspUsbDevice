# Examples

`EspUsbDevice` の基本的な使い方を確認するための Arduino sketches です。
このライブラリを使う sketch では Arduino-ESP32 標準の `USB.begin()`、
`USBHIDKeyboard`、`USBHIDMouse` は使いません。

## Keyboard

HID boot keyboard device の例です。詳しくは [Keyboard/README.ja.md](Keyboard/README.ja.md) を参照してください。

- `EspUsbDeviceConfig` で port、speed、VID/PID、string descriptor を設定します。
- `EspUsbDeviceHidKeyboard::write()` で US ASCII 文字列を送信します。
- `tapKey()` で 1 文字、`tapUsage()` / `pressUsage()` で raw HID usage ID を送信します。
- `setLayout()` で Host 側と同じ layout ID を選び、文字から usage への変換を切り替えます。
- `onOutputReport()` で NumLock / CapsLock / ScrollLock などの LED 状態を受け取ります。

文字列 wrapper は Host 側と同じ keymap table を逆引きし、Host 側と同じ layout ID を使います。

## Mouse

HID boot mouse device の例です。
詳しくは [Mouse/README.ja.md](Mouse/README.ja.md) を参照してください。

- `EspUsbDeviceHidMouse::move()` で移動、wheel、button 状態を送信します。
- `click()` で press / release の組を送信します。
- left / right / middle / back / forward button を raw button bit として扱います。

## KeyboardMouse

keyboard と mouse を同時に登録する composite HID device の例です。
現時点の composite HID は、単一 HID interface と report ID で構成します。
詳しくは [KeyboardMouse/README.ja.md](KeyboardMouse/README.ja.md) を参照してください。

- keyboard report ID: `1`
- mouse report ID: `2`
- composite HID endpoint MPS: `16 bytes`

## Gamepad

HID gamepad device の例です。
詳しくは [Gamepad/README.ja.md](Gamepad/README.ja.md) を参照してください。

- `EspUsbDeviceHidGamepad` で axes、hat switch、button bitmask を送信します。
- `send()` で 6 axes、hat、32 buttons をまとめて送れます。
- PC の game controller 設定画面や EspUsbHost の `onGamepad()` で確認できます。

## MediaKeys

HID consumer control / system control device の例です。
詳しくは [MediaKeys/README.ja.md](MediaKeys/README.ja.md) を参照してください。

- `EspUsbDeviceHidConsumerControl` で volume、mute、play/pause、track 操作を送信します。
- `EspUsbDeviceHidSystemControl` で power / standby / wake usage も扱えます。
- system control key は Host の電源状態に影響するため、自動送信しない example にしています。

## VendorHID

Vendor-defined HID device の例です。
詳しくは [VendorHID/README.ja.md](VendorHID/README.ja.md) を参照してください。

- `EspUsbDeviceHidVendor` で 63 byte payload の Input / Output / Feature report を扱います。
- Device から Host へ Input report を定期送信します。
- Host からの Output / Feature report を callback で受け取り、Serial monitor に出力します。
- 専用 Host application や EspUsbHost との小さな独自プロトコルに向いています。

## USBVendor

HID ではない vendor-specific USB interface の例です。
詳しくは [USBVendor/README.ja.md](USBVendor/README.ja.md) を参照してください。

- `EspUsbDeviceVendor` で bulk IN / OUT endpoint を使う独自通信を扱います。
- `available()` / `read()` / `write()` / `flush()` を使う stream 風 API です。
- `onControlRequest()` で EP0 の vendor request を受け取ります。
- `EspUsbDeviceConfig` で WebUSB landing URL を設定できます。

## CustomHID

任意の HID report descriptor を使う custom HID device の例です。
詳しくは [CustomHID/README.ja.md](CustomHID/README.ja.md) を参照してください。

- sketch 内で `REPORT_DESCRIPTOR` を定義します。
- `EspUsbDeviceHidCustom` に descriptor と input report size を渡します。
- 1 秒ごとに 8 byte Input report を送信します。
- 独自 descriptor の検証や、小さな固定長 HID report の試作に向いています。

## Serial

USB CDC ACM serial device の例です。
詳しくは [Serial/README.ja.md](Serial/README.ja.md) を参照してください。

- `EspUsbDeviceCdcSerial` で PC / Host とテキストを送受信します。
- `available()` / `read()` / `write()` / `print()` / `printf()` を使います。
- Host からの line coding と DTR / RTS 状態を callback で受け取ります。
- USB CDC とログ用 Serial monitor を分けて扱います。

## MIDI

USB MIDI device の例です。
詳しくは [MIDI/README.ja.md](MIDI/README.ja.md) を参照してください。

- `EspUsbDeviceMidi` で Note On / Off、Control Change などを送信します。
- 4 byte USB-MIDI event packet を raw に送受信できます。
- Host から受信した MIDI packet を Serial monitor に出力します。
- DAW、MIDI monitor、EspUsbHost などで確認できます。

## MIDIController

ADC と button input を USB MIDI message に変換する例です。
詳しくは [MIDIController/README.ja.md](MIDIController/README.ja.md) を参照してください。

- analog input を MIDI CC に変換します。
- button press / release を Note On / Off に変換します。
- potentiometer や BOOT button で簡単な MIDI controller を作れます。

## MIDIInterface

UART MIDI 1.0 と USB MIDI 1.0 を相互変換する bridge example です。
詳しくは [MIDIInterface/README.ja.md](MIDIInterface/README.ja.md) を参照してください。

- 31250 baud serial MIDI を USB-MIDI event packet に変換します。
- Host からの USB-MIDI event packet を serial MIDI byte stream に戻します。
- DIN MIDI 機器と USB MIDI host の bridge として使えます。

## AudioSink

USB Audio speaker sink device の例です。
詳しくは [AudioSink/README.ja.md](AudioSink/README.ja.md) を参照してください。

- `EspUsbDeviceAudioSink` で Host からの speaker PCM を受け取ります。
- `onData()` callback で PCM chunk を受信します。
- `onEvent()` callback で volume、mute、sample rate、interface enable を受け取ります。
- I2S bridge や codec 初期化はこのライブラリの責務外です。受信した PCM はアプリケーション、
  PCMFlow、PCMFlowDevice などへ渡せます。

## MSC

USB Mass Storage Class device の例です。
詳しくは [MSC/README.ja.md](MSC/README.ja.md) を参照してください。

- `EspUsbDeviceMsc` で SCSI inquiry、media 状態、write 可否を設定します。
- `EspUsbDeviceMscRamDisk` で RAM buffer を 512 byte block device として公開します。
- `disk.attach(msc)` で read/write callback と `msc.begin()` をまとめて設定します。
- この example は SCSI / block I/O 疎通確認用で、FAT でフォーマットされた USB メモリではありません。

MSC は block device と filesystem が分かれます。Host から通常のドライブとしてマウント
させたい場合は、有効な FAT image か SD card などの実 block storage が別途必要です。
flash / SPIFFS / LittleFS の直接公開は標準 example では扱わず、実用的な永続ストレージは
SD card を優先します。RAM disk は、FAT helper を追加すると firmware、設定ファイル、
Wi-Fi 転送用の一時ファイル受け渡しにも使えます。

## MSCFatRamDisk

RAM 上に小さい FAT12 image を作る MSC device の例です。
詳しくは [MSCFatRamDisk/README.ja.md](MSCFatRamDisk/README.ja.md) を参照してください。

- `EspUsbDeviceMscFatRamDisk` で Host から mount できる RAM disk を作ります。
- `README.TXT` を初期ファイルとして配置します。
- Host が `CONFIG.TXT` をコピーして eject した後、Device 側で file scan / read します。
- firmware update、設定ファイル投入、Wi-Fi 転送などの一時ファイル受け渡しの基本形です。

## MSCSdCard

SPI 接続の SD card を MSC block device として公開する例です。
詳しくは [MSCSdCard/README.ja.md](MSCSdCard/README.ja.md) を参照してください。

- `EspUsbDeviceMscSdCard` で Arduino `SD` の raw sector I/O を MSC に接続します。
- Host から SD card を通常の USB storage として読み書きします。
- Host が SD を所有している間、ESP32 側では `SD.open()` などの file API を使わない設計にします。
- 実用的な永続ストレージ example の基本形です。

## 注意

- USB device として使う側の ESP32-S3 などを USB host に接続してください。
- Serial monitor はログ確認用です。USB HID device としての接続先とは別の経路で確認してください。
- 既存の Arduino USB class と同時に使う設計ではありません。
