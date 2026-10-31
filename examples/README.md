# v3 examples / サンプル

v3の公開APIだけを使う利用例です。`build_opt.h`、内部header、TinyUSBの呼出しは使いません。
容量は各Functionの`configure()`で、USBの開始は`device.begin()`で指定します。
実機試験用のprobeは[validation](../validation/README.ja.md)にあり、これらの例とは別物です。

These examples use only the v3 public API: no `build_opt.h`, internal headers or
TinyUSB calls. Capacities are set with each Function's `configure()`, and USB
starts with `device.begin()`. Hardware validation probes live separately in
[validation](../validation/README.ja.md).

| Example | 内容 | What it demonstrates | API guide |
| --- | --- | --- | --- |
| [Keyboard](Keyboard/Keyboard.ino) | debug serialから入力した文字をキーとして送る | Debug serial input → HID key taps | [HID](../docs/V3_HID_API.ja.md) |
| [CdcConsole](CdcConsole/CdcConsole.ino) | CDCを`Stream&`として渡し、送信の空きを確かめてecho | CDC passed directly to a `Stream` consumer | [CDC](../docs/V3_CDC_API.ja.md) |
| [VendorEcho](VendorEcho/VendorEcho.ino) | Vendor bulkのecho。一部しか書けなかった残りを保持して送る | Vendor bulk echo, retaining partial writes | [Vendor](../docs/V3_VENDOR_API.ja.md) |
| [MidiEcho](MidiEcho/MidiEcho.ino) | MIDI 1.0 packetのecho（cable 0） | MIDI 1.0 packet echo | [MIDI](../docs/V3_MIDI_API.ja.md) |
| [CompositeCdcMidi](CompositeCdcMidi/CompositeCdcMidi.ino) | CDCとMIDIを同時に公開。容量はFunctionごと | CDC + MIDI with independent resources | [CDC](../docs/V3_CDC_API.ja.md), [MIDI](../docs/V3_MIDI_API.ja.md) |
| [MscCdcBlocks](MscCdcBlocks/MscCdcBlocks.ino) | RAMのraw block deviceとCDC。callbackの回数を表示 | RAM-backed raw blocks + CDC callback counts | [MSC](../docs/V3_MSC_API.ja.md), [CDC](../docs/V3_CDC_API.ja.md) |
| [AudioPlayback](AudioPlayback/AudioPlayback.ino) | UAC2のplayback。選ばれたformatを表示し、PCMを読み出す | UAC2 playback; reports the selected format | [Audio](../docs/V3_AUDIO_API.ja.md) |
| [ReconfigureVendor](ReconfigureVendor/ReconfigureVendor.ino) | `end → configure → begin`で、再ビルドせずringの容量を切り替える | Switches ring sizes at run time | [Vendor](../docs/V3_VENDOR_API.ja.md) |

## ビルドと書込み / Build and upload

```sh
arduino-cli compile --profile esp32s3 examples/CdcConsole
arduino-cli upload --profile esp32s3 --port <debug-port> examples/CdcConsole
python3 tools/build_check.py esp32s3   # 全exampleをまとめてbuild
```

- 各`sketch.yaml`はArduino-ESP32 3.3.12に固定し、このcheckoutのlibraryを読み込みます。profileは`esp32s3`（既定）、`esp32s2`、`esp32p4`です。
- `tools/build_check.py`の生成物は`build/examples/<profile>/<sketch>/`に置かれます。
- このlibraryが使うOTG controllerでは、CoreのUSB CDC/MSC/DFU on bootを有効にしないでください。

Each `sketch.yaml` pins Arduino-ESP32 3.3.12 and loads the library from this
checkout. Profiles: `esp32s3` (default), `esp32s2`, `esp32p4`. Do not enable the
Core's USB CDC/MSC/DFU-on-boot features on the OTG controller this library uses.

## 結線 / Connections

- boardの**native USB OTG**のdata口をHostへつなぎます。USB-UART bridgeは書込みとdebug用で、ここで作るFunctionの通信には使いません。
- Keyboard、ReconfigureVendor、AudioPlayback、MscCdcBlocksは、別のdebug接続で`Serial`（115200）も使います。
- pin配置とVBUS回路はboardと使うcontrollerに合わせてください。

Connect the board's **native USB OTG** data port to the Host. A USB-UART bridge
is only for programming and debug output. Keyboard, ReconfigureVendor,
AudioPlayback and MscCdcBlocks also print to `Serial` at 115200 on that separate
connection.

## 各exampleの動作 / Behavior

**CdcConsole / CompositeCdcMidi**：serial terminalで開けます。local echoを切ると、返ってきた文字が見えます。
CompositeCdcMidiはCDCとMIDIを同時に公開します。
CDC opens in a serial terminal; turn off local echo to see the returned bytes.

**MidiEcho**：cable 0で受けたUSB MIDI packetをそのまま返します。音は鳴りません。Host側でMIDIを送受信するアプリを使ってください。
Echoes USB MIDI packets on cable 0; it does not synthesize sound.

**VendorEcho / ReconfigureVendor**：汎用のbulk interfaceです。serial terminalでは開けないので、Host側のアプリ（libusb、pyusbなど）でinterfaceとendpointを選んで生のbyteを送受信します。
streamはメッセージの区切りを保たず、MPSの倍数の書込みに自動でZLPも付けません。Host側は長さを決めて読んでください。
自動で通信する相手側の例は、validationのVendor probeを参照してください。
A serial terminal cannot open the vendor interface; use a Host application.
The stream does not preserve message boundaries or add ZLPs; use length-framed reads.

**ReconfigureVendor**：debug portへ`r`を送ると、RX/TX ringを128 byteと2048 byteの間で切り替えます。`end()`の成功を確かめてから設定を変え、実際の容量を表示します。
Hostからは切断と再列挙に見えるので、interfaceを開き直してください。古いstreamのデータは捨てられます。記述子は変わらず、アプリのsessionも引き継ぎません。
Send `r` on the debug port to alternate 128/2048-byte rings. The Host sees a
disconnect and re-enumeration and must reopen the interface.

**Keyboard**：Hostで入力先のeditorを選び、debug portへ文字を送るとキーを送ります。起動時に勝手に入力はしません。
Sends keys only in response to debug serial input.

**AudioPlayback**：UAC2のplaybackだけを公開します。formatはstereoの16 bit/2 byteと24 bit/3 byte、rateは24 kHzと16 kHzです。
受け取ったPCMを読み捨て、選ばれたformatと読んだ量をdebug portへ表示します。音を出すには、読んだPCMをアプリでcodec/DACへ渡してください。
ringは最大の1 frame（6 byte）×32 frame＝192 byteで、event queueは使いません（`eventCapacity = 0`）。
Consumes PCM and reports the selected format; route `read()` data to your codec/DAC for sound.

**MscCdcBlocks**：8 KiB（512 byte×16 block）のRAMを**raw block device**として公開し、CDCでread/write callbackの呼出し回数を表示します。
初期内容は0で、filesystemはなく、resetで消えます。永続storageを使うときはcallbackを置き換えてください。
MSCの`transferBytes`は512で、FSでもHSでも使える値です（HSでは512の倍数が必要）。RAMの保存領域とこの512 byteのstagingは別に確保します。
CDC consoleを開くと毎秒、または`?`を送ると回数を返します。USBのpacket数ではありません。debug `Serial`はsetupエラーの表示用です。
Exposes 8 KiB of zero-filled RAM as a raw block device without a filesystem.
The MSC `transferBytes` of 512 is valid at both FS and HS.

これらは基本の使い方を示す例で、全クラスの一覧や、protocol・所有権の試験の代わりではありません。
開始に失敗した場合はUSBを止めたままにします。エラー処理は[開始と移行のガイド](../docs/V3_MIGRATION.ja.md)、
借用転送、callback、他のクラス、詳しい設定は[API guides](../README.md#api-documentation)を参照してください。

These are focused examples, not a catalog of every class or a substitute for the
protocol and ownership tests. See the [API guides](../README.md#api-documentation).

## 確認の範囲 / What has been checked

- 例そのものは**compileだけ**を確認しています。USB実機での動作確認は、例とは別のvalidation probeで行っています（S3 FS、P4/P4X FS・HSの状況は[README](../README.md#hardware-status-by-target-2026-10-31)）。
- 最初の6例はCDCのStream化の後、Core 3.3.12でS3/S2/P4の18 buildを確認しました。
- AudioPlaybackはPCM alternate対応の後に3 profileでbuildし、ELF、FQBN、未選択クラスがリンクされないことも確認しました。
- MscCdcBlocksも3 profileでcompileし、ELF/FQBN/依存を確認しました。未選択のAudio/NCM/Video/CCID/keymapやFAT helperはリンクされていません。記録は`validation/.pytest-results/msc-cdc-example*`。

The examples themselves are compile-checked only. USB behavior is covered by the
separate validation probes; see the [hardware status](../README.md#hardware-status-by-target-2026-10-31).
