# サンプルの進め方

[English](example-workflow.md)

EspUsbDevice v3の例を、配線から結果の確認、改造まで順に試すための案内です。

ESP32はUSB機器（Device）になる側です。PCや別のHostへつなぎ、キーボード、シリアルポート、音声機器などとして認識させます。相手にはHostが必要です。Device同士をつないでも列挙は始まりません。

USBの原理と切り分けの方法は[USB Device開発ガイド](usb-device-guide.ja.md)にあります。この文書は、例を手元で動かすときの手順と注意に絞っています。

## 先に知っておく用語

| 用語 | この案内での意味 |
| --- | --- |
| Device | USB全体の所有者。スケッチでは `EspUsbDevice` |
| Function | Hostに見せる機能1つ。`EspUsbDeviceHidKeyboard`、`EspUsbDeviceCdcSerial` など |
| 列挙（enumeration） | Hostが機器の情報を読み、addressと構成を決める手順 |
| descriptor | 機器が「どんな機能とデータ形式を持つか」を申告する情報 |
| interface | 1つのUSB機器の中の機能の単位。複合機器は複数持つ |
| endpoint | データの出入口。番号、方向、転送の種類を持つ |
| IN／OUT | 方向はHostから見た名前。INはDevice→Host、OUTはHost→Device |
| callback | 先に登録した関数を、ライブラリがeventに応じて呼ぶ仕組み |
| USB worker | USBの処理を行うFreeRTOS task。callbackはここで動く |

## まず試す例

1. [`EspUsbDeviceBringUpCheck`](../examples/Info/EspUsbDeviceBringUpCheck/README.ja.md)で、`begin()` の成功と、Hostの構成完了（`ready()`）を分けて確かめます。
2. [`Serial`](../examples/Serial/README.ja.md)で、ログ用の `Serial` と、新しく現れるCDCのポートを区別します。
3. [`Keyboard`](../examples/Keyboard/README.ja.md)や[`Gamepad`](../examples/Gamepad/README.ja.md)を、対応するPCのアプリで確かめます。
4. [`EspUsbDeviceDescriptorDump`](../examples/Info/EspUsbDeviceDescriptorDump/README.ja.md)で、宣言したFunctionと、interface・endpointの関係を読みます。
5. 複合機器、音声、ストレージ、firmware更新は、単機能を確かめてから進めます。

全体の一覧は[examples](../examples/README.md)にあります。各例のREADMEに、要る部品、操作、使うAPI、改造時の注意を書いています。補助ファイルを持つ例は、フォルダごと使ってください。

## 導入とbuild

1. [README](../README.ja.md)の手順で、Arduino-ESP32とライブラリを用意します。基準はArduino-ESP32 **3.3.12**です。
2. Arduino IDEなら「ファイル → スケッチ例 → EspUsbDevice」から例を開き、正確なboardを選びます。CLIなら、リポジトリのルートで次のようにbuildします。

```sh
arduino-cli compile --profile esp32s3 examples/Serial
arduino-cli upload  --profile esp32s3 --port <書込み用のport> examples/Serial
```

3. 各例の `sketch.yaml` が、profileごとのbuild条件です。

| profile | 対象 | 主な設定 |
| --- | --- | --- |
| `esp32s3` | ESP32-S3 | `USBMode=default`（USB-OTG (TinyUSB)） |
| `esp32s2` | ESP32-S2 | 既定 |
| `esp32p4` | ESP32-P4（旧revision v1.x） | `ChipVariant=prev3` |
| `esp32p4x` | ESP32-P4X（revision v3.x） | `ChipVariant=postv3` |

4. Arduino IDEで手で設定する場合は、上と同じにします。S3では `USB Mode` を「USB-OTG (TinyUSB)」にし、`USB CDC On Boot` などのOn Boot設定は無効のままにします。
5. 知っている書込み経路でuploadし、スケッチの `Serial.begin()` と同じ速度でログを開きます。

buildが通ったことは、配線や相手との動作を確かめたことにはなりません。ESP32-S2はcompileだけを確かめていて、実機では未確認です。

## コネクタと電源を先に確かめる

基板の「USB」という表示だけでは、OTGのデータ端子、USB Serial/JTAG、USB-UARTブリッジのどれにつながるかわかりません。回路図で、controller、D−/D+、PHY、VBUS、USB-CのCC回路を確かめます。データ線入りのケーブルを使います。

- **Hostは**相手にVBUSを供給する側、**Deviceは**Hostから受け取る側です。外部電源とUSBの電源を不用意につながず、boardの給電構成に従います。
- **ESP32-P4には、HSとFSのcontrollerがあります。** 既定（`EspUsbController::Auto`）はHSです。ケーブルを挿したコネクタと `config.controller` を合わせてください。合っていないと、`begin()` は成功してもHostには何も見えません。controllerの選択は、基板の配線や給電回路を変えません。
- **`Serial` はスケッチのログの経路です。** boardと設定によって、UART経由にもUSB Serial/JTAG経由にもなります。どの端子で読めるかを確かめてください。例が作るCDCやHIDとは別のものです。
- OTGとSerial/JTAGのpinを入れ替える例（P4のFS）では、先にUARTなど別のログ・復旧の経路を用意します。

## コードを読む順序

1. 先頭の説明と、端子、形式、命令の定数を読みます。値の単位を確かめます。
2. global objectの関係を見ます。`EspUsbDevice`（全体の所有者）と、それに登録するFunctionやhelperを区別します。
3. `setup()` で、callbackの登録 → Functionの `configure()` → `device.begin()` の順を追います。どれも `begin()` の前に行います。
4. `loop()` で、受信済みのbyte、状態の変化、定期の処理、保留している仕事を見ます。
5. 戻り値の種類を区別します。成否の `bool`、`esp_err_t`、実際に処理したbyte数、状態の照会があります。失敗したとき何を保持し、何を捨てるかは例ごとに違います。

## 状態を取り違えない

| 観察点 | 読み方 |
| --- | --- |
| `device.begin(config)` が成功 | 資源を確保し、USBを開始した。Hostの列挙はまだ終わっていないことがある |
| `device.started()` | `begin()` 成功から `end()` まで。Hostとの接続とは無関係 |
| `device.ready()` | HostがSET_CONFIGURATIONを済ませた |
| `device.suspended()` | busが休止している。`ready()` はtrueのまま |
| `device.speed()` | 最後のbus resetでHostと決まった速度（`Full`か`High`）。それより前は `None` |
| CDCの `connected()` | HostのDTRの状態。物理的な接続や、ほかのFunctionの利用とは別 |
| Audio／Videoのstream開始 | Hostがそのstreamを使い始めた段階。列挙だけでは流れない |
| send／writeの成功 | APIが受け付けた。Hostのアプリが処理したこととは別 |

`begin()` が失敗したら、すぐに `device.lastErrorName()` を表示します。どのコードでも `esp_err_to_name()` の名前を返します。主な意味は次のとおりです。詳しくは[開発ガイドのエラーの読み方](usb-device-guide.ja.md#27-エラーの読み方)を見てください。

| 表示 | まず疑うこと |
| --- | --- |
| `ESP_ERR_INVALID_SIZE` | endpointの予算超え。IN endpointがcontrollerのTX FIFOに入らない。HSでの大きさの条件（MSCの `transferBytes` が512の倍数でない、など） |
| `ESP_ERR_NO_MEM` | heapの不足 |
| `ESP_ERR_INVALID_ARG` | 設定値の誤り |
| `ESP_ERR_INVALID_STATE` | 二重の `begin()`。動作中に追加したFunction |
| `ESP_ERR_NOT_SUPPORTED` | USB OTGのないchip。ないcontrollerを指定した |

## 設定を変えるとき

容量や方式は、Functionごとの `configure()` で決めます。build flagや `build_opt.h` は使いません（TinyUSBの旧flagはcompile errorになります）。

- 変えるときは `device.end()` → `configure()` → `device.begin()` の順です。Hostからは、抜いて挿し直したように見えます。
- 動作中に `configure()` やFunctionの追加はできません。
- FSで動いた設定が、P4のHSでは `begin()` で失敗することがあります。HSではbulkの最大packetが512 byteになるからです（[HSで変わる値](usb-device-guide.ja.md#43-hsで変わる値)）。
- Functionを足すと、endpointとFIFOの予算に当たることがあります。S3では、Device→Host（IN）のendpointが4本までです（[endpointの予算](usb-device-guide.ja.md#44-endpointの予算)）。

## callbackとloop()を分ける

USBのcallbackは、`loop()` とは別のUSB workerで動きます。`loop()` から手で呼んで受信する形ではありません。callbackに長いログ、待機、decode、file I/Oを足すと、次のUSB処理が遅れます。

- callbackは短くします。重い解析、ファイル処理、再起動の判断は、例が示す経路で `loop()` 側へ渡します。
- callbackの中で待ちません。HIDの送信は `timeoutMs = 0` にします。
- callbackの中で `begin()`／`end()` や `configure()` を呼びません。
- callbackに渡されたdata、report、frameは、戻ると再利用されることがあります。後で使う分は自分のbufferへコピーし、持ち主と寿命を決めます。
- 借用転送（`writeDirect`、`readDirect`、Videoの `submitFrame` など）のbufferは、受け付けられてから完了通知が来るまで、変更も解放もしません。コピーする通常のwriteと混同しないでください。`writeDirect`／`readDirect` のbufferは、DMA可能な内部RAMに64 byte境界で置きます。PSRAMは使えません。

別のtaskとの受け渡しに `volatile` だけを使っても、排他、順序、複数の値の整合は保証されません。FreeRTOSのqueueなど、同期された受け渡しを使います。callbackからは待たずに投入し、満杯のときに捨てるか、数えるか、やり直すかを決めておきます。

## 有限のbufferと失敗の扱い

- readは、返ってきた長さだけを解釈します。要求した長さが全部届くことや、1回のcallbackが1命令であることを仮定しません。
- writeが短かった、またはfalseだった場合は、残りを保持するか、捨てたことを数えるかを決めます。例のログ表示だけでは回復になりません。
- HIDは押下と解放、MIDIはNote OnとOff、Videoはframeを借用している期間、Audioはsample・frame・byteの違いを確かめます。
- storageでは、blockの操作とfilesystemを区別します。Hostとスケッチで、同じ媒体を同時に書き換えません。
- callbackの中で大量にprintして速度を測らないでください。表示の遅さとUSBの遅さを分けます。

## うまくいかないとき

| 症状 | 次に確かめること |
| --- | --- |
| ログ自体が読めない | boardの設定、`Serial` の経路、端末の通信速度、port切替の影響 |
| `begin()` が失敗する | `lastErrorName()` の表示、controllerの設定、他のUSB stackとの競合、要る資源 |
| 開始は成功するが列挙されない | データ端子、ケーブル、VBUS、選んだcontrollerとコネクタ（P4） |
| 列挙するがclassが使えない | interface、endpoint、report descriptor、相手が対応する形式 |
| 最初だけ動く、負荷で欠ける | 実際の受信長、返ったbyte数、queueの満杯、callbackの時間、bufferの寿命 |
| 再接続の後に動かない | 古いsession、DTR、押したままのキー、保留した転送の状態 |

相手なし → 接続 → 通常の操作 → 切断 → 再接続、の順に1つずつ試し、各段階のログを残します。機器固有の設定の変更、印刷、Flashの書換えなどは、何が実際に起きるかを各例のREADMEで確かめてから行います。

## 確認の範囲について

examplesは、v3の公開APIだけを使う利用例です。実機での合否の試験は、別の[validation](../validation/README.ja.md)で行っています。確かめた環境は、S3 2台（FS）、P4 1台のHS↔FS、旧P4とP4XのHS直結、P4XとLinux PC（HS）です。S2の実機、Windows、macOSでのv3の動作は未確認です。

詳しい原理は[USB Device開発ガイド](usb-device-guide.ja.md)、内部の仕組みとcallbackの文脈は[上級編](usb-device-advanced.ja.md)、classごとのAPIは `docs/classes/` の各ガイドを見てください。
