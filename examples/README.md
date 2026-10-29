# v3 examples / サンプル

These examples use the v3 public API, with no `build_opt.h`, internal headers or
TinyUSB calls. They are freshly written usage examples, separate from the
[hardware validation probes](../validation/README.ja.md).

v3の公開APIだけを使う新規サンプルです。旧examplesの互換性を基準にせず、独立した
validation probeとも分けています。容量はconfigure、USB開始はDeviceのbeginで指定します。

| Example | What it demonstrates / 内容 |
| --- | --- |
| [Keyboard](Keyboard/Keyboard.ino) | Debug serial input → HID key taps / debug portから入力した文字をキー送信 |
| [CdcConsole](CdcConsole/CdcConsole.ino) | CDC passed directly to a Stream consumer / Stream参照へ渡し、TX空きを確認してecho |
| [VendorEcho](VendorEcho/VendorEcho.ino) | Vendor bulk stream, retaining partial writes / partial writeの残りを保持 |
| [MidiEcho](MidiEcho/MidiEcho.ino) | MIDI 1.0 packet echo, one cable each way / MIDI packet単位のecho |
| [CompositeCdcMidi](CompositeCdcMidi/CompositeCdcMidi.ino) | CDC + MIDI with independent resources / 個別の容量を持つ複合構成 |
| [MscCdcBlocks](MscCdcBlocks/MscCdcBlocks.ino) | Raw RAM-backed blocks + CDC callback counts / RAM block callbackとCDC、機能別の容量指定 |
| [AudioPlayback](AudioPlayback/AudioPlayback.ino) | Runtime PCM formats/rates and currentFormat, consuming samples / PCM形状・rateと選択状態を照会して読み出す |
| [ReconfigureVendor](ReconfigureVendor/ReconfigureVendor.ino) | `end → configure → begin` switches ring sizes in one binary / 再ビルドせず容量を切替 |

## Board and connections / 環境と結線

Profiles pin Arduino-ESP32 3.3.12. `esp32s3` is the primary development profile;
`esp32s2` and `esp32p4` are compile targets, not evidence of hardware validation.
Each profile loads the library from this checkout. Do not enable the Core's
USB CDC/MSC/DFU-on-boot features on the OTG controller used by this library.

profileはCore 3.3.12に固定し、このcheckoutのlibraryを読み込みます。
標準の開発実機はS3です。S2/P4 profileがあることは実機確認済みを意味しません。
このlibraryが使うOTG controllerでCore標準USBの自動起動を有効にしないでください。

Connect the board's **native USB OTG** data port to the Host. A USB-UART bridge
is a separate programming/debug connection; it does not carry these Functions.
Keyboard, ReconfigureVendor and AudioPlayback also use `Serial` at 115200 on that separate
connection. Board pinout and VBUS circuitry must match the chosen controller.
Use an editor on the Host for Keyboard; it sends keys only in response to debug
serial input, rather than typing automatically at startup.

データ用native OTGをHostへ結線します。USB-UART bridgeは書き込み・debug用で、
ここで実装したFunctionの通信経路にはなりません。Keyboard/ReconfigureVendor/AudioPlaybackは
別のdebug接続でSerial 115200も使います。boardごとのpinout/VBUS回路を確認してください。
KeyboardはHostで入力先のeditorを選び、debug portへ文字を送るとキーを送信します。

```sh
arduino-cli compile --profile esp32s3 examples/CdcConsole
arduino-cli upload --profile esp32s3 --port <debug-port> examples/CdcConsole
python3 tools/build_check.py esp32s3
```

The build helper stores output under `build/examples/<profile>/<sketch>/`.
The initial six examples compiled for S3/S2/P4 with Core 3.3.12 (18 builds) after the
native CDC Stream change. This verifies build compatibility, not USB operation
on S2/P4.

ビルドhelperの生成物は`build/examples/<profile>/<sketch>/`へ保存します。
CDCのStream化後、Core 3.3.12で最初の6例×S3/S2/P4の全18ビルドを確認しました。
S2/P4のUSB実機動作を確認した意味ではありません。

AudioPlayback subsequently compiled for all three profiles after PCM alternate support.
Its ELF architecture, FQBN and selected/unselected symbols were checked. These
three builds are separate from the earlier 18-build baseline.

AudioPlaybackはPCM alternate対応後にS3/S2/P4の3 profileでビルドし、ELF/FQBNと
未使用クラスのリンク除外を確認しました。先の18ビルドとは別の実行です。

## Operation / 動作

CDC can be used with a serial terminal; turn off local echo to see the returned
bytes. CompositeCdcMidi exposes CDC and MIDI at the same time. MIDI echoes USB
MIDI packets and does not synthesize sound. Use a Host application that sends
and receives MIDI on cable 0.

CDCはserial terminalで利用でき、local echoを無効にすると返ってきた文字を確認できます。
複合例はCDCとMIDIを同時に公開します。MIDIはcable 0のpacketを返す例で、音源ではありません。

VendorEcho/ReconfigureVendor expose a vendor-specific bulk interface. The Host
must discover the advertised interface/endpoints and transfer raw bytes; a
serial terminal cannot open it. The stream does not preserve message boundaries
or implicitly terminate MPS-multiple writes with a ZLP. Use length-framed Host
reads. For a complete automated peer interaction, see the independent Vendor
probe in validation.

Vendorは汎用bulk interfaceなので、Host側でdescriptorからinterface/endpointを選び
生byteを転送します。serial terminalでは開けません。streamはmessage境界や暗黙ZLPを
保証しないので、Host側は予定byte数などで区切ります。自動ピア通信はvalidationのVendor probeを参照してください。

For ReconfigureVendor, send `r` on the debug port. It alternates 128/2048-byte RX
and TX rings, checks `end()` before changing resources, and prints effective
capacities. The Host sees disconnect/re-enumeration, must reopen the interface,
and should treat old stream data as discarded. It does not change descriptors
or preserve an application session across restart.

ReconfigureVendorはdebug portの`r`でRX/TX ringを128/2048 byteへ交互に切り替えます。
end成功後に再設定し、実際の容量を表示します。Host側は切断・再列挙後に開き直し、
旧stream dataを破棄します。application sessionの継続を実装する例ではありません。

AudioPlayback exposes UAC2 playback with stereo 16-bit/2-byte and 24-bit/3-byte
PCM, each at 24/16 kHz. It consumes received PCM and reports the selected format
and bytes read on the debug port. It does not generate audible sound: route the
read data to your application's codec/DAC for that. Its 32-frame ring reserves
192 bytes for the largest declared frame, and event polling is disabled.

AudioPlaybackはUAC2のPlaybackのみを選び、stereoの16 bit/2 byteと24 bit/3 byteを
各24/16 kHzで公開します。PCMを読み捨て、選択formatと読み出し量をdebug portへ出す例です。
音声を鳴らす場合はアプリ側で読み出したPCMをcodec/DACへ渡します。
ringは最大6 byte/frame×32 frameの192 byteで、event queueは使いません。

These are focused examples, not a catalog for every class or a replacement for
protocol/ownership tests. Startup errors leave USB stopped. See the
[lifecycle/migration guide](../docs/V3_MIGRATION.ja.md) for error handling and the
[API guides](../README.md#api-documentation) for borrowed transfer ownership,
callbacks, other classes and detailed configuration.

基本的な利用例に絞っており、全クラスのcatalogやprotocol/所有権試験の代用ではありません。
開始に失敗した場合はUSBを停止したままにします。借用転送、callback、他クラスの詳細は各APIガイドを参照してください。


MscCdcBlocks exposes an initially zero-filled **raw block device** and a CDC
console. The 8 KiB RAM backing store has no filesystem, loses its contents on
reset, and is separate from the 512-byte MSC transfer buffer. Replace the block
callbacks to use application storage. The CDC console reports read/write
callback counts once per second when opened, or when sent `?`; these are not
USB packet counts. Debug `Serial` at 115200 reports setup errors separately.

MscCdcBlocksは8 KiBのRAM領域をraw block deviceとして公開し、CDCでread/write callbackの
呼出し数を表示します。初期内容は0で、filesystemはなく、resetで内容が消えます。
RAM保存領域とMSCの512 byte stagingは別の費用です。永続storageを使う場合はcallbackを置き換えます。
CDC consoleを開くと毎秒、または`?`を送ると回数を返します。USB packet数ではありません。
別のdebug Serial 115200はsetupエラーの表示用です。filesystem helperはこの例の必須依存にしていません。

MscCdcBlocks compiled for all three profiles with Core 3.3.12; ELF/FQBN/source
checks also passed. No unselected Audio/NCM/Video/CCID/keymap or FAT helper was
linked. This is compile-only evidence for the example, separate from the
independent CDC/MSC hardware probes. Records: `validation/.pytest-results/msc-cdc-example*`.

MscCdcBlocksの3 profile compileとELF/FQBN/依存確認は成功し、未選択機能の混入はありません。
この例自体のUSB実機・OSでのmount確認を表す結果ではなく、独立したCDC/MSC実機probeとは分けています。
