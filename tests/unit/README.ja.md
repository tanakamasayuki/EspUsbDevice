# Unit テスト

> English: [README.md](README.md)

unit テストでは、ホストに依存しないロジックを検証します。

- device descriptor の byte 列。
- configuration descriptor layout。
- FS / HS endpoint MPS 選択。
- HID report descriptor の byte 列。
- HID keyboard / mouse report builder。
- MSC FAT RAM disk helper の boot sector、FAT、root directory、file read helper。

## `compile_smoke`

最初の環境確認用テストです。`--run-mode=build` で Arduino CLI、sketch.yaml、
ESP32 board package、ライブラリ解決、公開ヘッダの最小コンパイルを確認します。
USB device stack の実行確認ではありません。

## `descriptor`

USB device / configuration / HID report descriptor の byte 列を検証します。
初期仕様として、HID keyboard と HID mouse の interrupt endpoint MPS は FS / HS とも
8 bytes に固定します。keyboard + mouse composite は単一 HID interface + report ID 構成で、
report ID 付き keyboard report に合わせて endpoint MPS を 16 bytes にします。

## `descriptor_model`

v2のdescriptor基盤をhost g++だけで検証します。Arduino/TinyUSB headerには依存せず、
buffer境界、interface/string採番、direction別endpoint採番、duplex endpoint、重複・上限検出、
FS/HS configuration descriptorのMPS切替、other-speed configuration、device qualifier、
HID function writerを確認します。Arduino sketch全体が一時的にcompile不能でも単独実行できます。

## `tinyusb_config`

ライブラリ所有のTinyUSB設定をS2/S3/P4の各target macroでhost compileし、Arduino Coreの
Kconfigに依存せず全device classが有効になること、S2/S3はFS、P4はFS/HS対応能力として
compileされること、Audioのcompile-time上限を確認します。controller/root-hub portと
実際のbus speedはこの設定では固定せず、runtime初期化で選択します。

## `tinyusb_vendor`

Arduino build対象のTinyUSB headerと選択したdevice sourceが、固定commitのarchiveと
byte-identicalであること、および意図しない`.c`がbuild対象へ増えていないことを確認します。

## `audio_model`

旧Audio実装に依存しないv2のPCM format/bandwidth modelをhost上で検証します。
mono/stereo、16/24/32 bit、subslot、FS/HS frame rate、clock tolerance、
isochronous packet上限、software buffer上限、entity graph、UAC2 descriptor、
Clock/Feature control stateとCUR/RANGE wire formatを確認します。

## `audio_v2_descriptor`

新公開APIの`EspUsbAudioFunction`をS3実機上で構築し、speaker、microphone、duplexの
configuration descriptor、FS/HS packet size、mute / volume / stream state eventの
polling、stream statsのreset lifecycleを確認します。USB runtimeは開始しないため、
純粋な公開API・device descriptor・control state統合テストです。

## `keymap`

board 不要の純粋な host g++ テストです。実行時に layout enum、
`ESP_USB_DEVICE_MOD_*` 定数、keymap include 群、そして純粋な逆引き関数
`espUsbDeviceAsciiToUsage` を実ソース `src/EspUsbDevice.{h,cpp}` からそのまま抽出し、
`keymap_test.cpp` と一緒にコンパイルして、文字 -> HID usage + modifier の
round-trip を検証します。base / Shift 段、AltGr（Right Alt）フォールバック
（de_DE の `@`、`{ [ ] }` 等）、および pt_BR の 0x90 tableSize 修正
（International1 の `/` `?` = usage 0x87）を確認します。`src/keymap/*.h` の
テーブルは EspUsbHost と byte-identical で、順方向は EspUsbHost 側の keymap テストが
カバーしています。

## `fat_ramdisk`

`EspUsbDeviceMscFatRamDisk` の host 非依存ロジックを検証します。

- FAT12 boot sector の基本 field。
- volume label、FAT type、boot signature。
- 8.3 filename の正規化。
- root directory entry。
- FAT12 cluster chain。
- `exists()`、`fileSize()`、`readFile()`。
- `EspUsbDeviceMsc` への attach、read/write callback、eject callback。
