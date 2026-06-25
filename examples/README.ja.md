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

## 注意

- USB device として使う側の ESP32-S3 などを USB host に接続してください。
- Serial monitor はログ確認用です。USB HID device としての接続先とは別の経路で確認してください。
- 既存の Arduino USB class と同時に使う設計ではありません。
