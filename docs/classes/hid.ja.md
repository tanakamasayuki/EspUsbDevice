# HID（Human Interface Device）

[English](hid.md)

## 概要と使う場面

HID は、キーボード、マウス、ゲームパッドなどの入力機器のための USB class です。
Windows、Linux、macOS のどれにも標準のドライバがあるので、Host 側に何も入れずに使えます。

このライブラリには次の HID Function があります。

| クラス | 用途 | report |
| --- | --- | --- |
| `EspUsbDeviceHidKeyboard` | キーボード。6キー同時押し（既定）か NKRO | boot 8 byte / NKRO 29 byte |
| `EspUsbDeviceHidMouse` | マウス（5ボタン、X/Y、ホイール） | report 4 byte / boot 3 byte |
| `EspUsbDeviceHidGamepad` | ゲームパッド（6軸、hat、32ボタン） | 11 byte＋ID |
| `EspUsbDeviceHidConsumerControl` | 音量、再生などのメディアキー | 2 byte＋ID |
| `EspUsbDeviceHidSystemControl` | 電源オフ、スタンバイ、ウェイク | 1 byte＋ID |
| `EspUsbDeviceHidVendor` | 独自データの送受信（vendor 定義の usage） | `reportSize` byte＋ID |
| `EspUsbDeviceHidCustom` | 自分で書いた report descriptor | descriptor で決まる |

使う場面の目安です。

- キー入力やポインタ操作を Host に送りたい：Keyboard、Mouse、Gamepad、Consumer/System Control。
- ドライバなしで独自データをやり取りしたい：`EspUsbDeviceHidVendor`。速度は bulk より低いですが、Windows でもドライバの導入が要りません。
- 速度が必要：HID ではなく [Vendor bulk](vendor.ja.md) を使います。

HID Function を2つ以上登録すると、1つの HID interface と1つの report descriptor にまとめられ、report ID で区別されます（[複合 HID](#複合-hid)）。

動作は、ESP32-S3 同士の FS 接続と、ESP32-P4/P4X の HS 直結（どちらも EspUsbHost を Host にした試験）で確認しています。PC の OS との相互接続は未確認です。

## 基本の使い方

```cpp
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);
EspUsbDeviceHidMouse mouse(device);

void setup() {
  keyboard.onOutputReport([](const EspUsbDeviceHidKeyboardOutputReport &leds) {
    // USB worker で呼ばれる。待つ処理や end() はしない。
  });
  if (!device.begin()) {
    // 失敗理由は device.lastError() / device.lastErrorName()
  }
}

void loop() {
  if (device.ready()) {
    keyboard.write("hello\n");   // 文字列をキー操作として送る
    mouse.move(10, 0);           // 右へ 10
  }
  delay(1000);
}
```

- Device を先に作り、Function を作ってから `device.begin()` を呼びます。各 Function は Device が開始します。Function の `begin()` は公開していないので、`keyboard.begin()` と書くと compile error になります。
- callback の登録と設定は `device.begin()` の前に行います。
- 送信関数の `true` は「送信を始めた」という意味です。Host が受け取ったことは意味しません。

## 設定

HID には設定用の構造体はありません。constructor の引数と、停止中に呼ぶ設定関数で決めます。

| 項目 | 既定値 | 範囲・意味 | FS/HS の違い |
| --- | --- | --- | --- |
| `EspUsbDeviceHidVendor(device, reportSize)` | 63 | 1 report の payload の byte 数（report ID を除く）。1〜`maxReportSize()`（65534） | interrupt の packet 長は、FS で `reportSize + 1` と 64 の小さい方（最小 8）。HS では `reportSize + 1` が 64 を超えると、その値（最大 1024） |
| `EspUsbDeviceHidCustom(device, desc, len, inputReportSize)` | 64 | `desc` は report descriptor。`inputReportSize` は interrupt IN の `wMaxPacketSize` として宣言する値（report ID の 1 byte を含む）。これより長い report も複数の packet に分けて送る。0 は begin が `ESP_ERR_INVALID_ARG` | FS は最大 64。HS では 64 を超える値をそのまま使う（最大 1024。下の表） |
| `keyboard.enableNkro(true)` | 無効 | NKRO（usage 0x00〜0xDF の bitmap、29 byte）。停止中だけ | なし |
| `keyboard.setLayout(layout)` | `ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_US` | `write()`/`tapKey()` で使う文字→キーの変換表。動作中も変えられる | なし |
| 送信関数の `timeoutMs` | 100 | IN endpoint が使用中のときに待つ時間（ms）。0 は待たない | なし |

### endpoint と packet 長

| 構成 | interrupt IN | interrupt OUT | packet 長（FS） | packet 長（HS） | bInterval |
| --- | --- | --- | --- | --- | --- |
| Keyboard 単独 | あり | あり（LED） | 8（NKRO は 32） | 同左 | 1（FS 1 ms、HS 125 µs） |
| Mouse、Gamepad、Consumer、System 単独 | あり | なし | 8 | 8 | 同上 |
| Custom 単独 | あり | なし | `inputReportSize`（8〜64） | 64 を超えれば `inputReportSize`（最大 1024）。それ以外は FS と同じ | 同上 |
| Vendor 単独 | あり | あり | `reportSize + 1`（8〜64） | `reportSize + 1`（最大 1024） | 同上 |
| 複合 HID（HID Function が2つ以上） | 1本を共有 | 1本を共有 | 16、32（NKRO Keyboard）、Vendor の `reportSize + 1`、Custom の `inputReportSize` のうち最大（それぞれ 64 まで） | FS の値と、各 Vendor／Custom の HS の値のうち最大（最大 1024） | 同上 |

- report が packet 長より長いときは、1回の転送で複数の packet に分けて送ります（USB 2.0 §5.7.3）。たとえば単独の Gamepad（ID 込み 12 byte）は 8 byte と 4 byte の packet になります。
- 単独の Keyboard と Mouse は boot interface（subclass 1）として宣言します。複合 HID は report ID を使うので boot interface にはなりません（[規格との対応](#規格との対応と設計の理由)）。
- 非 control の IN endpoint は、全 Function 合わせて ESP32-S3 で 4本、P4 の HS controller で 7本までです。HID は何個登録しても IN を1本しか使いません。
- どの HID Function でも、`inputPacketSize()` で、Host と決まった速度での interrupt IN の `wMaxPacketSize` を得られます（bus reset の前と end の後は 0）。
- HS で 512 byte を超える interrupt packet を使うと、Host 側の周期転送用 FIFO が足りないことがあります。ESP32-P4 の EspUsbHost を Host にする場合は、Host 側で `ESP_USB_HOST_FIFO_LARGE_PERIODIC_OUT` を指定してください。

## API

### 送信の共通ルール

- 送信関数（`sendReport`、`sendInput`、`tapKey`、`move` など）は `bool` を返します。`true` は内部 buffer へコピーして転送を始めたという意味です。
- IN endpoint が使用中なら最大 `timeoutMs` 待ちます。
- 失敗の理由は `device.lastError()` で分かります。

| `device.lastError()` | 原因 |
| --- | --- |
| `ESP_ERR_INVALID_STATE` | 未接続、Host が構成していない、bus が休止中、停止処理中、USB worker（callback）の中から `timeoutMs` 付きで呼んだ |
| `ESP_ERR_INVALID_SIZE` | 長さが report の宣言を超える、boot protocol で長さが合わない |
| `ESP_ERR_TIMEOUT` | `timeoutMs` の間 IN endpoint が空かなかった |
| `ESP_ERR_INVALID_ARG` | report で表せない値（下の各 Function を参照）。何も送らない |

呼べる文脈：

- 通常の task からは `timeoutMs` 付きで呼べます。
- USB worker で動く callback の中からは `timeoutMs = 0` で呼びます。待ち時間付きの呼び出しは拒否されます。
- 割込み（ISR）からは呼ばないでください。

同じ内容でも、明示的に呼んだ送信は必ず送ります。間引きはしません。

### Keyboard

| 関数 | 説明 |
| --- | --- |
| `write(text, interKeyDelayMs = 5)` | ASCII 文字列を1文字ずつ押して離す。変換できない文字で false |
| `tapKey(ch, holdMs = 10)` / `pressKey(ch, timeoutMs = 100)` / `releaseKey(ch, timeoutMs = 100)` | 1文字を押して離す / 押す / 離す。`releaseKey` は、配列がその文字に使う modifier も離す |
| `tapUsage(usage, modifiers = 0, holdMs = 10)` / `pressUsage(usage, modifiers = 0, timeoutMs = 100)` / `releaseUsage(usage, timeoutMs = 100)` | HID usage ID（`ESP_USB_HID_KEY_*`）で操作する |
| `releaseAll()` | すべて離す |
| `sendReport(EspUsbDeviceBootKeyboardReport)` | 6キーの boot 形式（8 byte）を送る |
| `sendReport(EspUsbDeviceNkroKeyboardReport)` | 全キーの状態を送る。`enableNkro()` を begin 前に呼んでいないと false |
| `ledState()` | Host が最後に送った LED 状態（Num/Caps/Scroll Lock、Compose、Kana）。callback がなくても更新される。bus reset や抜去で消える |
| `onOutputReport(cb)` / `onProtocol(cb)` / `enableNkro()` | 停止中だけ `ESP_OK`。動作中は `ESP_ERR_INVALID_STATE` で、設定は変わらない |
| `protocol()` | 現在の protocol（0＝boot、1＝report） |
| `heldState()` | NKRO で最後に Host へ伝えた状態。NKRO 無効のときは空 |

- `setLayout()` は「どの文字をどのキー位置で送るか」を決めるだけです。画面に出る文字は Host 側のキーボード配列の設定で決まります。両者を合わせてください。
- `press*` は押下中の状態に足し、`release*` はそこから外します。キーは重なります。`pressUsage(A)` の後に `pressUsage(B)` を呼ぶと、両方を押したままになります。`modifiers` の modifier も、押下中のものに足します。
- modifier の usage（0xE0〜0xE7、Left Control〜Right GUI）は、キー欄を使わず modifier の bit を立てます。
- NKRO なしでは、modifier 以外のキーを 6 個まで押したままにできます（boot report は 6 個の配列。HID 1.11 Appendix B.1）。7 個目は `ESP_ERR_INVALID_SIZE` で失敗し、状態は変わりません。キーを離すと、そのキーだけを外し、残りを前へ詰めます。
- `tap*` は、自分が足したものだけを離します。tap の前から押していたキーや modifier は押したままです。
- 配列で打てない文字では、`pressKey`/`tapKey`/`releaseKey`/`write` が `ESP_ERR_NOT_SUPPORTED` で失敗します。NKRO では、bitmap で表せない usage（0xDF より大きく modifier でないもの）が `ESP_ERR_INVALID_ARG` で失敗します。
- NKRO が有効なら、`pressKey`/`pressUsage` も NKRO の状態を使います。
- NKRO が有効でも、Host が boot protocol を選んでいる間（BIOS/UEFI など）は6キーの boot 形式に変換して送ります。

### Mouse

| 関数 | 説明 |
| --- | --- |
| `move(x, y, wheel = 0, buttons = KeepButtons, timeoutMs = 100)` | 相対移動。既定の `KeepButtons`（-1）は `press()` で押したボタンを保つので、`press()` の後の `move()` はドラッグになる。0〜255 を渡すと、`press()`/`release()` と同じく押下中のボタンをその値にする。それ以外の値は `ESP_ERR_INVALID_ARG` |
| `wheel(w)` | ホイールだけ動かす。ボタンの状態は保つ |
| `press(b)` / `release(b)` / `click(b, holdMs = 10)` / `releaseAll()` | ボタン（`ESP_USB_DEVICE_MOUSE_LEFT`/`RIGHT`/`MIDDLE`/`BACK`/`FORWARD`） |
| `sendReport(EspUsbDeviceBootMouseReport)` / `sendState()` | report をそのまま送る / 今のボタン状態を送る |

report protocol では 4 byte（ボタン、X、Y、ホイール）、boot protocol では 3 byte に変換して送ります。

### Gamepad

`send(x, y, z, rz, rx, ry, hat, buttons)` または `sendReport(EspUsbDeviceGamepadReport)` で送ります。軸は -127〜127、ボタンは32個（bit 0 がボタン1）です。

hat（方向キー）は `ESP_USB_DEVICE_GAMEPAD_HAT_CENTER`（0＝押していない）と、`HAT_UP`（1）から時計回りに `HAT_UP_LEFT`（8）までの定数で指定します。1〜8 は 0〜315 度に対応します。

### Consumer Control

`press(usage)` / `release()` / `click(usage)` / `sendUsage(usage)` に Consumer ページの usage ID を渡します。descriptor が宣言する範囲は 0〜0x3FF（Logical Maximum 1023）です。これより大きい値は `ESP_ERR_INVALID_ARG` で失敗し、送りません。
`ESP_USB_DEVICE_CONSUMER_CONTROL_VOLUME_UP`、`MUTE`、`PLAY_PAUSE`、`NEXT_TRACK` などの定数があります。

### System Control

`press` / `click` / `sendUsage` には Generic Desktop ページの標準 usage ID を渡します。

| 定数 | 値 |
| --- | --- |
| `ESP_USB_DEVICE_SYSTEM_CONTROL_POWER_OFF` | 0x81 |
| `ESP_USB_DEVICE_SYSTEM_CONTROL_STANDBY` | 0x82 |
| `ESP_USB_DEVICE_SYSTEM_CONTROL_WAKE_HOST` | 0x83 |
| （離す） | 0 |

それ以外の値は false を返し（`ESP_ERR_INVALID_ARG`）、何も送らず、状態も変えません。
`WAKE_HOST` は普通の HID 入力です。休止中の Host を起こす remote wakeup とは別のものです（remote wakeup は `device.remoteWakeup()`）。

### Vendor（生データ）

| 関数 | 戻り値・制約 |
| --- | --- |
| `sendInput(data, length, timeoutMs = 100)` | 送信を始めたら true。`length` が 0 または `reportSize()` を超えると false（切り詰めない）。短い payload はそのまま送る |
| `onOutputReport(cb)` / `onFeatureReport(cb)` | 停止中だけ `ESP_OK`、動作中は `ESP_ERR_INVALID_STATE` |
| `reportSize()` | constructor で指定した payload 長 |
| `maxReportSize()` | 65534（内部の転送長が 16 bit で、report ID の 1 byte を除いた値） |

- `maxReportSize()` は確保が成功することを保証しません。使えるメモリと Host 側の対応も制約になります。
- `reportSize` が上限を超えると begin が `ESP_ERR_INVALID_SIZE`、0 なら `ESP_ERR_INVALID_ARG` で失敗します。
- 受け手が固定長を求める場合は、アプリ側で長さをそろえてください。
- 単独で使う場合も report ID 6 を付けて送ります。Host 側の read では先頭 1 byte が report ID です。

### Custom（独自の report descriptor）

```cpp
static const uint8_t kReport[] = { /* report descriptor */ };
EspUsbDeviceHidCustom custom(device, kReport, sizeof(kReport), 64);
// begin 後
custom.sendReport(data, length);       // reportId 0 = 単独なら番号なし、複合なら割り当てた ID
```

- `kReport` の配列は動作中も有効で、変更しないまま保ってください。コピーはしません。
- begin で descriptor を解析し、Report Size / Count / ID と global の Push/Pop から、ID と種類（Input/Output/Feature）ごとの長さを求めます。
- 次のものは begin で拒否します：途中で切れた item、Push/Pop の不整合、Report ID 0 の明示、番号付きと番号なしの report の混在、転送長の上限超過。
- これは usage や意味をすべて検査するものではありません。descriptor の正しさは自分で確かめてください（[Host OS 側](#host-os-側の扱い)の確認方法を参照）。

### Feature report の値

```cpp
uint8_t value[] = {1, 2, 3};
esp_err_t error = pipe.setFeatureReport(value, sizeof(value));
```

Feature report の現在値を設定します。Host が次に GET_REPORT したときに返ります。interrupt 送信や `onFeatureReport` の通知は起きません。

- begin 成功後に、アプリ task からも USB worker からも呼べます。Host の構成前に設定した値も残ります。
- payload に report ID は含めません。`reportId` を省略（0）すると、その Function の ID を選びます。Feature の ID が複数ある Custom HID では ID を明示します。

| 戻り値 | 条件 |
| --- | --- |
| `ESP_OK` | 設定した |
| `ESP_ERR_INVALID_ARG` | `data` が null か `length` が 0、別 Function の ID、ID が一意に決まらない |
| `ESP_ERR_INVALID_STATE` | 開始前、または停止処理中 |
| `ESP_ERR_NOT_SUPPORTED` | その ID に Feature report がない |
| `ESP_ERR_INVALID_SIZE` | 宣言した長さを超える |

### 受信（Output / Feature）

- Output report は interrupt OUT（ある場合）と SET_REPORT の両方で受けます。Feature は SET_REPORT で受けます。
- callback には `EspUsbDeviceHidReport`（`reportId`、`reportType`、`data`、`length`）が渡ります。`data` はその callback の間だけ有効です。残すならコピーします。
- callback は USB worker で呼ばれます。待つ処理、`device.end()`、Function の破棄、設定変更はしないでください。

### 容量の照会

`inputReportCapacity()` / `outputReportCapacity()` / `featureReportCapacity()` は、共有 interface に必要な最大 byte 数を返します。report ID を宣言した report は ID の 1 byte を含みます。開始前と終了後は 0 です。

`inputPacketSize()` は、Host と決まった速度での interrupt IN の `wMaxPacketSize` を返します。複合 interface ではすべての HID Function で同じ値です。Host が bus reset をするまでと、end の後は 0 です。

### 独自の HID クラスを作る

`EspUsbHidFunction` を基底にします。begin/end、転送、確保エラー、停止は基底が受け持ちます。

- `begin()` は `protected` の欄で override します。呼ぶのは `device.begin()` で、sketch からは呼びません。その中で検証し、基底の `begin()` を呼んで、その結果を返します。
- 派生クラスが callback や応答データを持つ場合は、派生 destructor の先頭で `end()` を呼びます。worker が止まってからメンバーが破棄されるようにするためです。

## Host からの要求への応答

ライブラリ独自の HID driver が次を処理します。アプリが手を入れる必要はありません。

| 要求 | 動き |
| --- | --- |
| GET_DESCRIPTOR（HID、Report） | HID descriptor と report descriptor を返す |
| GET_REPORT | ID と種類ごとに保存した最新の値を返す。初期値は 0 埋め |
| SET_REPORT（Output/Feature） | 長さと ID を検査し、合わなければ STALL。合えば callback へ渡す |
| GET_IDLE / SET_IDLE | report ID ごとに管理。0 は自動再送なし（既定） |
| GET_PROTOCOL / SET_PROTOCOL | boot interface のときだけ。bus reset で report protocol に戻る |
| SET_INTERFACE（alt 0）、CLEAR_FEATURE(ENDPOINT_HALT) | 転送状態を初期化し、data toggle を DATA0 に戻す |

- **GET_REPORT の値**：Input は送信を受け付けたとき、Output/Feature は SET_REPORT が完了したときに更新します。短い payload を送ると、保存する長さも短くなります。
- **idle 再送**：Host が SET_IDLE で指定した値×4 ms ごとに、その Input の保存値を再送します（HID 1.11 §7.2.4）。マウスの X/Y/ホイールのような相対値は、送信後に 0 に戻した値を再送するので、同じ移動を繰り返しません。
- **protocol の切り替え**：Keyboard は boot 8 byte と NKRO 29 byte、Mouse は boot 3 byte と report 4 byte の間で切り替わります。送信中の古い report は止めてから応答するので、形式の違う report が Host へ届くことはありません。
- **bus reset と構成解除**では Input/Output を中立の値に戻し、idle を無効にします。Feature の値は保ちます。
- **SET_INTERFACE や HALT の解除**で、受け付け済みでも未完了の report は捨てられることがあります。確実な配送が必要なら、Host とアプリの間で確認と再送をしてください。

## バッファとメモリ

begin のときに report descriptor から必要な大きさを求め、必要な領域だけを確保します。ビルド設定は要りません。

| 領域 | 確保する条件と大きさ |
| --- | --- |
| IN の DMA 領域 | Input report があるとき。最大 Input report 長 |
| OUT の DMA 領域と組み立て領域 | Output report と interrupt OUT があるとき。packet 長と最大 Output report 長 |
| 状態の保存領域 | 定義した ID/種類ごとの report 長の合計（GET_REPORT と idle 再送用） |
| control 応答用の領域 | Input/Output/Feature の最大長 |

- 確保に失敗すると begin が `ESP_ERR_NO_MEM` で失敗し、確保済みの領域をすべて戻します。
- 転送中に heap は使いません。`device.end()` は USB worker と DMA を止めてから解放します。
- DMA 領域はライブラリが内部 RAM に確保します。HID には利用者が buffer を渡す API はないので、PSRAM を指定する場所もありません。
- HID を使わないプログラムには、HID の driver と buffer はリンクされません。

## Host OS 側の扱い

HID は各 OS の標準ドライバで動きます。下の内容は各 OS の一般的な仕組みです。この版のライブラリでの PC との相互接続は未確認です。

| OS | 標準ドライバ | アプリからの生データの読み書き |
| --- | --- | --- |
| Windows | HidUsb（キーボードは kbdhid、マウスは mouhid） | vendor 定義の report は HIDAPI などで開ける。キーボードとマウスは OS が専有するので、アプリからは開けない |
| Linux | usbhid（`hidraw`、`input`） | `/dev/hidraw*`。一般ユーザーで開くには udev 規則が要る |
| macOS | IOHIDFamily | HIDAPI など。キーボードの入力監視にはプライバシー設定の許可が要る |

確認の方法：

- Linux：`lsusb -v -d <vid>:<pid>` で descriptor、`sudo usbhid-dump` で report descriptor、`evtest` で入力イベントを確認できます。
- Windows：USB Device Tree Viewer で descriptor と状態を確認できます。
- 文字が違う：Host が自分の配列で usage を文字に変えています。`setLayout()` を Host の配列に合わせてください。
- BIOS/UEFI で使えない：Host は boot protocol を使います。Keyboard と Mouse は単独で登録し、boot interface にしてください。

## 規格との対応と設計の理由

- **HID 1.11 準拠**：HID descriptor の bcdHID は 1.11 です。boot interface は §4.2 と付録 B、idle は §7.2.4、protocol は §7.2.5〜7.2.6 に従います。
- **複合 HID を boot interface にしない理由**：boot 形式の report には report ID がありません（HID 1.11 付録 B）。report ID で複数の Function をまとめた interface は boot 形式と両立しないので、subclass/protocol を 0 にします。
- **Gamepad の hat**：logical range を 1〜8 にし、Input に Null 状態を付けて、0 を「押していない」にしています（HID 1.11 §6.2.2.5、HID Usage Tables の Hat Switch）。
- **System Control**：usage は array で、logical minimum 1、0 を「押していない」にしています（HID 1.11 §6.2.2.5 の推奨）。wire 上は 1/2/3 の選択値を送り、descriptor でそれを 0x81/0x82/0x83 に対応させています。
- **runtime 確保にした理由**：ビルド設定なしで大きな report を使えます。使わない OUT/Feature の資源を省けます。HID を使わないと HID class 全体がリンクされません。代わりに、begin での解析と確保、不正な descriptor や長すぎる送信の明示的な拒否が増えています。
- **idle 再送で相対値を 0 にする理由**：再送が新しい入力として解釈され、マウスが動き続けることを防ぐためです。

### 複合 HID

HID Function を2つ以上登録すると、1つの interface にまとめます。

- report ID は、まず各 Function の既定値を使います（Keyboard 1、Mouse 2、Gamepad 3、Consumer 4、System 5、Vendor 6）。同じ種類を2つ登録した場合や Custom HID には、空いている ID を登録順に割り当てます。
- 各 Function の report descriptor の最初の Collection の直後に、割り当てた Report ID を入れます（その位置にすでに Report ID があれば置き換えます）。Report ID を複数使う Custom HID は、単独で登録するのが確実です。
- 各 Function の descriptor 全体を global の Push/Pop で囲み、単位や範囲が次の Function に引き継がれないようにしています。このため Function ごとに 2 byte 長くなります。
- HID の interface は、他の class の interface より前に並びます。

## v2 からの変更点

- `CFG_TUD_HID`、`CFG_TUD_HID_EP_BUFSIZE` などのビルド定数は廃止しました。残っているとビルドが移行を促すエラーになります。
- System Control は標準 usage（0x81〜0x83）で指定します。整数 1/2/3 を直接書いていたコードは定数へ直してください。
- callback の登録関数は `esp_err_t` を返し、停止中だけ設定できます。
- 独自の HID は `EspUsbHidFunction` を基底にします。

詳しくは [v2 からの移行](../migrating-from-v2.ja.md) を参照してください。

## 関連する examples

| example | 内容 |
| --- | --- |
| [Keyboard](../../examples/Keyboard/) | 文字列、配列の切り替え、LED 状態 |
| [KeyboardNKRO](../../examples/KeyboardNKRO/) | NKRO（全キー同時押し） |
| [Mouse](../../examples/Mouse/) | マウスの移動とボタン |
| [KeyboardMouse](../../examples/KeyboardMouse/) | 複合 HID（report ID で区別） |
| [Gamepad](../../examples/Gamepad/) | 軸、hat、ボタン |
| [MediaKeys](../../examples/MediaKeys/) | Consumer Control |
| [CustomHID](../../examples/CustomHID/) | 独自の report descriptor |
| [VendorHID](../../examples/VendorHID/) | ドライバなしの生データ送受信 |
| [CompositeHidCdcMsc](../../examples/CompositeHidCdcMsc/) | HID と CDC、MSC の複合 |

規格：[USB HID 1.11](https://www.usb.org/sites/default/files/hid1_11.pdf)、[HID Usage Tables](https://usb.org/sites/default/files/hut1_7.pdf)
