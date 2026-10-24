# サンプルの進め方

[English](example-workflow.md)

EspUsbDeviceの例を、配線から結果の確認、改造まで順に試すための案内です。ESP32がUSB機器になる側です。PCや別のHostへ接続し、キーボード・シリアルport・音声等として認識させます。接続相手にはHostが必要で、Device同士をつないでも列挙は始まりません。

## 先に知っておく用語

| 用語 | この案内での意味 |
| --- | --- |
| 列挙（enumeration） | Hostが機器の情報を読み、addressと構成を決める手順 |
| descriptor | 機器が「どんな機能とデータ形式を持つか」を説明する情報 |
| interface | 一つのUSB機器の中の機能単位。複合機器は複数持つことがある |
| endpoint | データの出入口。番号・方向・転送方式を持つ |
| report / packet / frame | 各classやprotocolで決まるデータ単位。同じbyte数とは限らない |
| callback | 事前登録した関数を、libraryがevent等に応じて呼ぶ仕組み |
| buffer / queue | 後で処理するデータを保持する有限の領域・待ち行列 |
| timeout | 相手を待つ時間に上限を設け、応答なしを扱う条件 |

## まず選ぶ例

1. [BringUpCheck](../examples/Info/EspUsbDeviceBringUpCheck/README.ja.md)でbegin()とホストの設定完了を分けて確認します。
2. [Serial](../examples/Serial/README.ja.md)で、ログ用Serialと新たに現れるCDC portを区別します。
3. [Keyboard](../examples/Keyboard/README.ja.md)や[Gamepad](../examples/Gamepad/README.ja.md)を、対応するPCアプリで確認します。
4. [DescriptorDump](../examples/Info/EspUsbDeviceDescriptorDump/README.ja.md)で、宣言した機能とinterface・endpointの関係を読みます。
5. 複合機能・音声・ストレージ・更新は、単機能を確認してから進めます。

[全サンプル一覧](../examples/README.ja.md)から、各例の必要部品・操作・API・改造時の注意へ進めます。機種固有のhelperがある例は、そのフォルダ全体を使います。

## 導入とビルド

1. [library README](../README.ja.md)の導入手順でArduino-ESP32とライブラリを用意します。対応target・core・確認済み機器はそこで案内される対応資料を参照し、個別例のprofileも確認します。
2. Arduino IDEの「ファイル → スケッチ例 → EspUsbDevice」から例を開き、正確なboardを選びます。CLIならリポジトリのルートで次のようにビルドできます。

```sh
arduino-cli compile --profile esp32s3 examples/Serial
```

3. `sketch.yaml`のplatform・libraryはそのprofileのビルド条件です。ローカルlibraryを参照する設定も確認し、外部依存・生成アセットが必要な例は隣のREADMEの準備を行います。ファイル名だけコピーするとhelperが欠ける場合があります。
4. 既知の書き込み経路でuploadし、スケッチの`Serial.begin()`と同じ条件でログを開きます。CLIのビルド成功を、実配線や相互運用の確認とは扱いません。

## コネクタと電源を先に確認する

基板の「USB」という表示だけでは、OTGデータ端子・USB Serial/JTAG・USB-UART bridgeのどれにつながるか分かりません。回路図でcontroller・D−/D+・PHY・VBUS・USB-C CC/role回路を確認します。データ対応ケーブルを使い、端子の信号電圧を合わせます。

Hostは相手機器へ必要なVBUSを供給する側、DeviceはHostへつながる側です。外部電源とUSBの電源出力を不用意に結ばず、boardの給電構成に従います。P4のport・PHY切替は、コネクタの配線や給電回路を自動変更しません。

`Serial`はスケッチの診断経路です。選択board・設定によってUSB経由の場合もUART経由の場合もあるため、どの端子で読めるかを確認します。USB classのCDCやHIDとは別に考えます。Serial/JTAGとOTGの端子を切り替える例では、独立したUART等のログ・復旧経路を先に確保します。

## コードを読む順序

1. 先頭の概要と端子・format・command定数を読みます。値の単位を確認します。
2. global objectの関係を見ます。USB全体のownerと、それに登録する機能・helperを区別します。
3. setup()のcallback登録・媒体やformatの準備・begin()の順を追います。
4. loop()で、受信済みbyte、状態の変化、定期処理、保留している仕事を見ます。
5. 戻り値が成功boolか、実byte数か、状態の問い合わせかを区別します。失敗後に何を保持・破棄するかは例ごとに異なります。

## 状態を取り違えない

| 観察点 | 読み取り方 |
| --- | --- |
| `device.begin(config)`成功 | 内部開始。ホストの列挙はまだ終わっていない場合がある |
| `device.ready()` | ホストがconfigurationを設定した状態 |
| CDCの`connected()` | ホストDTRの状態。物理接続や他classの利用と同じではない |
| Audio/Videoのstream開始 | ホストがそのstreamを利用する段階。列挙だけでは流れない |
| send/write成功 | APIの受付・転送条件を満たした状態。ホストアプリの処理結果とは別 |

USBのIN/OUTはDeviceから見た方向です。INはDevice→Host、OUTはHost→Deviceです。要求と返信のどちらがどの方向を使うかはprotocolで決まり、送信者の気分で呼び名を入れ替えません。

## callbackとloop()を分ける

このライブラリのUSB callbackは、loop()とは別のUSB処理taskで動きます。登録した関数をloop()から手動で呼んで受信する形ではありません。長いログ・待機・decode・file I/Oをcallbackへ足すと、次のUSB処理を遅らせます。

受信通知だけでなく、HID Output・control要求・MSC/APDUの応答等もUSB処理に関わります。応答providerは長さと容量を検査して短く返し、通知からの重い解析・ファイル処理・再起動の判断は、例が示すアプリ側の経路へ分けます。

callbackのdata・report・frameは、戻ると再利用される場合があります。後で使う分を自分のbufferへコピーし、所有者と完了点を決めます。非同期・direct転送のbufferは受付後も転送が完了するまで変更しません。コピーする通常writeと、借りるdirect APIの契約を混同しないようにします。

別taskとの共有は`volatile`だけで排他・順序・複数fieldの整合が保証されるわけではありません。FreeRTOS queue等の同期された受け渡しを使い、callbackからは待たずに投入し、満杯時に破棄・計数・再試行のどれをするか決めます。共有領域を守る短いcritical sectionの中で、USB APIやSerialの待機を行わないようにします。

## 有限bufferと失敗の扱い

- readの返却長だけを解釈します。要求長が全部届くことや、一callbackが一命令であることを仮定しません。
- writeが短い・falseの場合は、未送信部分を保持するか、そのデータを捨てたことを数えるかを決めます。例のログ表示だけでは回復処理になりません。
- HIDは押下と解放、MIDIはNote OnとOff、videoはframeの所有期間、audioはsample/frame/byteの違いを確認します。
- storageのblock操作とfilesystemを区別します。ホストとスケッチで同じ媒体を同時に書き換えません。
- callbackで大量にprintして速度を測らないでください。表示の遅さとUSBの遅さを分けます。

## うまくいかないとき

| 症状 | 次の確認 |
| --- | --- |
| ログ自体が読めない | board設定、Serialの経路、端末の通信条件、port切替の影響 |
| begin()が失敗 | 直後のエラー名、controller設定、他stackとの競合、必要な資源 |
| 開始は成功するが列挙されない | データ端子、ケーブル、VBUS・役割、選択controllerとPHY |
| 列挙するがclassが使えない | interface・endpoint・report descriptor、相手の対応format |
| 最初だけ動く・負荷で欠ける | 実受信長、返却byte数、queueの満杯、callback時間、buffer寿命 |
| 再接続後に動かない | 古いaddress・session・DTR・保持キー・保留転送の状態 |

相手なし、接続、通常操作、切断、再接続の順で一つずつ試し、各段階のログを控えます。device-specificな設定変更・印刷・Flash更新等は、どの実動作が起きるかを個別READMEで確認してから行います。

詳しい原理は[USB開発ガイド](usb-device-guide.ja.md)、callbackや転送の実装契約は[上級ガイド](usb-device-advanced.ja.md)、公開APIは[library README](../README.ja.md)を参照してください。版ごとの対応や実測値は、それぞれの専用資料へ集約します。
