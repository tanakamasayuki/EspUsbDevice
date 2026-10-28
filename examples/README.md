# v3 examples / サンプル

These examples use the v3 public API, with no `build_opt.h`, internal headers or
TinyUSB calls. They are freshly written usage examples, separate from the
[hardware validation probes](../validation/README.ja.md).

v3の公開APIだけを使う新規サンプルです。旧examplesの互換性を基準にせず、独立した
validation probeとも分けています。容量はconfigure、USB開始はDeviceのbeginで指定します。

| Example | What it demonstrates / 内容 |
| --- | --- |
| [Keyboard](Keyboard/Keyboard.ino) | Debug serial input → HID key taps / debug portから入力した文字をキー送信 |
| [CdcConsole](CdcConsole/CdcConsole.ino) | CDC byte echo with backpressure / TX空きを確認してCDC echo |
| [VendorEcho](VendorEcho/VendorEcho.ino) | Vendor bulk stream, retaining partial writes / partial writeの残りを保持 |
| [MidiEcho](MidiEcho/MidiEcho.ino) | MIDI 1.0 packet echo, one cable each way / MIDI packet単位のecho |
| [CompositeCdcMidi](CompositeCdcMidi/CompositeCdcMidi.ino) | CDC + MIDI with independent resources / 個別の容量を持つ複合構成 |
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
Keyboard and ReconfigureVendor also use `Serial` at 115200 on that separate
connection. Board pinout and VBUS circuitry must match the chosen controller.
Use an editor on the Host for Keyboard; it sends keys only in response to debug
serial input, rather than typing automatically at startup.

データ用native OTGをHostへ結線します。USB-UART bridgeは書き込み・debug用で、
ここで実装したFunctionの通信経路にはなりません。Keyboard/ReconfigureVendorは
別のdebug接続でSerial 115200も使います。boardごとのpinout/VBUS回路を確認してください。
KeyboardはHostで入力先のeditorを選び、debug portへ文字を送るとキーを送信します。

```sh
arduino-cli compile --profile esp32s3 examples/CdcConsole
arduino-cli upload --profile esp32s3 --port <debug-port> examples/CdcConsole
python3 tools/build_check.py esp32s3
```

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

These are focused examples, not a catalog for every class or a replacement for
protocol/ownership tests. Startup errors leave USB stopped. See the
[lifecycle/migration guide](../docs/V3_MIGRATION.ja.md) for error handling and the
[API guides](../README.md#api-documentation) for borrowed transfer ownership,
callbacks, other classes and detailed configuration.

基本的な利用例に絞っており、全クラスのcatalogやprotocol/所有権試験の代用ではありません。
開始に失敗した場合はUSBを停止したままにします。借用転送、callback、他クラスの詳細は各APIガイドを参照してください。
