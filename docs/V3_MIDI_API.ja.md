# v3 MIDIのpacket設定と転送

2026-10-26。USB-MIDI 1.0のクラス処理を共通bulk転送へ移し、TinyUSB MIDI classと固定FIFOを無効化した。
単一global instanceはなく、Functionごとに容量・cable数・通知方式を持つ。
通常のreadPacket/writePacket、noteOn/noteOff/controlChange等の呼出し方は維持する。

```cpp
EspUsbDevice device;
EspUsbDeviceMidi midi(device);

void setup() {
  EspUsbMidiConfig config;
  config.inCables = 16;
  config.outCables = 1;
  config.rxPackets = 1;
  config.txPackets = 4;
  config.transferPackets = 1;
  config.name = "MIDI Interface";
  if (midi.configure(config) != ESP_OK) return;
  device.begin();
}

void loop() {
  EspUsbDeviceMidiPacket packet;
  if (midi.readPacket(packet)) {
    // Consume one complete event. Copy it if another task needs it.
  }
}
```

設定変更はDevice.end→configure→Device.beginで行い、再ビルドしない。
configure/onPacketは停止中だけ成功し、拒否時は既存設定を保つ。
constructorの一つのcable数、またはIN/OUT二つのcable数を指定する形式も残す。
configureは全設定値を置換するので、constructorの値を引き継ぐ場合は`auto config = midi.configuration()`から変更する。

## 設定と容量

| 設定 | 意味 |
| --- | --- |
| inCables / outCables | device→host / host→deviceのvirtual cable数。各1〜16、packet headerのcableは0始まり |
| name | 任意のUTF-8 Function名。beginで文字列descriptorへcopyする |
| rxPackets / txPackets | event単位のring容量。1 eventはEspUsbDeviceMidiPacketの4 byte |
| transferPackets | copy TX一回の最大event数。0は選択controllerの1 bus packet、1なら4 byte投稿 |
| rxBuffer / txBuffer | 任意の外部event配列。指定ring容量分をbeginからendまで保持する |
| rxMode | Queue、Callback、Disabled。Callback/DisabledはrxPackets=0、rxBuffer=nullptr |
| autoFlush | 既定true。falseなら明示flushで送出を開始する |
| streamTermination | FunctionDefaultはEndWithShortPacket。ringの末尾がMPS倍数ならZLPも送る |

cable数はclampせず、範囲外を拒否する。旧コードで0/17以上を指定していた場合は修正が必要。
16という上限はUSB-MIDI 1.0の4 bit cable fieldによるもので、TinyUSB instance数の定数ではない。
native coreへの一回の転送投稿長はuint16_tなので、transferPacketsの明示指定は16383以下。使用可能RAM、endpoint数、Host実装も別の制約になる。

`availablePackets()`と`availableForWritePackets()`は、今読める/書ける完全event数を返す。
receiveCapacity/sendCapacityは確保したringのevent数、transferCapacityは実効TX staging容量をevent数で返す。
開始前/終了後は0。configurationは要求値なので、自動選択のtransferPackets=0も保持する。
nameの入力寿命とUTF-8制限は[文字列ガイド](V3_TEXT_DESCRIPTORS.ja.md)に従う。

RX ringより大きなUSB packetは、一つのDMA packet領域に残りを保持する。
readPacketでringを空けると残りを移し、全部読み終えるまで次のpacketをarmしない。
そのためring容量1でもMPS分のeventを失わず受けられ、availablePacketsはring容量を超える場合がある。
総受信保持量には、このDMA packet分も含まれる。外部ringをfreeすることや、ringを黙って拡張することはしない。

TX ringは4 byte単位で受理し、空きがなければwritePacketはfalseを返す。
複数taskが同時に送っても、一つのeventのbyteを混ぜたり一部だけ受理したりしない。
復帰後は元packetを再利用できる。成功はcopy受理であり、Hostの受信完了ではない。
txPackets=0ならTX ring/stagingを確保せず、writePacketは受理しない。

## callbackと省略する方向

callback受信は、停止中にonPacketを登録し、rxMode=CallbackとrxPackets=0を設定する。
callbackには完全なeventのconst参照を渡し、その呼出しの間だけ有効。別taskへ渡すなら4 byteをcopyする。
USB workerで実行するためblock、end、構成変更を行わない。callback内のwritePacketも非blockingで、
空きがなければfalseとなる。確実な転送が必要なら適切なTX容量かアプリ側の保留を用意する。

送信専用はrxMode=Disabled、rxPackets=0でRX ring/DMAを省略する。
受信専用はtxPackets=0でTX ring/DMAを省略する。両方向を省略しても大きなbufferは確保しない。
descriptor上のbulk pairは維持するため、endpointの予算は両方向とも必要。
MIDIを使わなければ、独自MIDI実装とTinyUSB MIDI FIFO自体をリンクしない。
共通stateには他のbulk Functionと共有する管理情報があり、全metadataがzero costという意味ではない。

## eventの検証と移行

writePacketは送信先cableの範囲と予約CIN 0/1を検証する。無効ならfalseを返し、queueへ入れない。
受信は無効eventと1〜3 byteの端数を取り除き、同じpacket内の有効eventは順番を保って渡す。
receiveErrorsは無効event一つまたは端数一つごとの累積数で、beginで初期化する。
これはMIDIメッセージ全体やSysEx sequenceのsemantic検証ではなく、有効eventのpayloadは変更しない。
既存のchannel/valueをclampする便利helperの動作は維持する。

`CFG_TUD_MIDI_*`のFIFO/endpoint buffer指定と、CFG_TUD_MIDI（0を含む）は削除する。
外部コードがTinyUSBのtud_midi_*を直接呼んでいた場合も、Functionのpacket APIへ移す。
MIDI 2.0/UMPはこのFunctionの対象外。

利点はビルド設定なしの再構成、複数Function、方向ごとの資源省略、小さいqueue、typed callback。
不利益はbegin時の動的確保と、MPS未満のringで小分けにcopyする処理、無効cable/CINを拒否する移行負担である。
共通bulkのcopy RX/TXもMPS未満のring・転送量を使えるようになった。

## S3–S3の確認

新規validation/s3/midi_streamは容量1、37/43、外部ring、callback、片方向、2 MIDI、MIDI+CDC+Keyboardを実通信する。
2つ目のMIDIはHostの標準MIDI APIが第一interfaceだけを扱うため、標準の明示interface bulk APIから確認する。
Host側へ専用patchを入れず、同じS3二台だけを使う。
全11試験は`runtime-midi-all.xml`で11 passed、0 skipped、0 unfinished、586.92秒。
最後にTX検証をbulk lock内へ揃えた後、`midi-stream-final.xml`で関連3試験が成功した（192.03秒）。
2 taskから計200 eventの順序・完全性、313取得点の失敗/回復、Vendorの容量/transfer 1 byteを含む。
IN16/OUT1の非対称構成でcable15への送信も確認し、全helper、clamp、SysEx/realtimeの15 eventをbyte照合した。
常時送信taskとendを重ね、停止中にcable数を32回変更しても終了後の送信が受理されないことも確認した。
全11件と最終3件の全caseで両board STOPPED、panicなし。
4最小buildのnmで未使用Function、未使用取消し、TinyUSB CDC/Vendor/HID/MIDIの不在も確認した。
P4/S2/S31の実機、HS、各OSの音楽アプリとの相互接続は今回の結果には含まない。

規格参照: [USB-MIDI 1.0](https://www.usb.org/sites/default/files/midi10.pdf)。


## byte I/Oとの型の分離

MIDIはPrintでもStreamでもない。従来のPrint upcastでの拒否をruntimeで返す形から、
不適切な型変換をcompile時点で拒否する形へ変更した。共通転送層からPrintのstateも外す。
送信余地はavailableForWritePackets()でevent単位に照会し、writePacket/noteOn等を使う。
共有bulkを実装に使うことは、MIDIの公開操作をbyte streamにする意味ではない。
