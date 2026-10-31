# HID API（v3）

Keyboard、Mouse、Gamepad、Consumer Control、System Control、Vendor（生データ用）、Custom（任意の report descriptor）の HID Function です。
TinyUSB の HID class は使わず、ライブラリ独自の HID driver で動きます。転送領域は report descriptor から必要な大きさを求めて begin 時に確保するので、ビルド設定は要りません。
複数の HID Function を登録すると、1つの HID interface と1つの report descriptor にまとめられ、report ID で区別されます。

## 基本の使い方

```cpp
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);
EspUsbDeviceHidVendor pipe(device, 257);  // 1 report あたり 257 byte の生データ

void setup() {
  Serial.begin(115200);
  pipe.onOutputReport([](const EspUsbDeviceHidReport &report) {
    // report.data はこの callback の間だけ有効。必要ならコピーする。
  });
  if (!device.begin()) {
    Serial.println(device.lastErrorName());
  }
}

void loop() {
  keyboard.tapKey('a');
  uint8_t data[257] = {1, 2, 3};
  pipe.sendInput(data, sizeof(data));  // 送信 buffer へコピーできたら true
  delay(1000);
}
```

Device を先に作り、Function を作ってから `device.begin()` を呼びます。各 Function の `begin()` は Device が呼ぶので、アプリから重ねて呼ぶ必要はありません。

### 主な設定

HID には設定用の構造体はありません。constructor の引数と、停止中に呼ぶ設定関数で決めます。

| 項目 | 既定値 | 意味 | FS/HSでの違い |
| --- | --- | --- | --- |
| `EspUsbDeviceHidVendor(device, reportSize)` | 63 | 1 report の payload の byte 数（report ID を除く）。上限 `maxReportSize()` = 65534 | interrupt endpoint の packet 長は FS で最大 64 byte。HS では `reportSize + 1` が 64 を超えると、その値（最大 1024）を HS 用 descriptor に載せる |
| `EspUsbDeviceHidCustom(device, desc, len, inputReportSize)` | 64 | `inputReportSize` は packet 長の希望。転送領域の大きさは descriptor から求める | 同上の packet 上限（FS 64、HS 1024） |
| `keyboard.enableNkro(true)` | 無効（6キー同時押し） | NKRO（全キー同時押し、bitmap 形式）。停止中に呼ぶ | なし |
| `keyboard.setLayout(layout)` | `ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_US` | 文字→キーの変換表 | なし |
| 送信関数の `timeoutMs` | 100 | IN endpoint が使用中のときに待つ時間（ms） | なし |

1つの report が packet 長より大きい場合は、複数の packet に分けて1回の転送で送ります（例：FS で 258 byte の report は 64 byte の packet 5個）。

## 送信の共通ルール

- 送信関数（`sendReport`、`sendInput`、`tapKey`、`move` など）の true は「内部 buffer へコピーして転送を始めた」という意味です。Host が受け取ったことは意味しません。
- endpoint が使用中なら最大 `timeoutMs` 待ちます。callback の中から送るときは `timeoutMs = 0` にしてください。USB worker の中から待ち時間付きで送ろうとすると拒否されます。
- `EspUsbDeviceHidVendor::sendInput()` は、`reportSize` より長い payload を切り詰めずに false を返します。短い payload はそのまま送れます。受け手が固定長を求める場合は、アプリ側で長さをそろえてください。
- bus が休止（suspend）している間は新しい Input を拒否します。
- 同じ値でも、明示的に呼んだ送信は必ず送ります（idle 設定で間引くことはしません）。

## Keyboard

- `sendReport(EspUsbDeviceBootKeyboardReport)`：6キーの boot 形式（8 byte）を送る。
- `sendReport(EspUsbDeviceNkroKeyboardReport)`：全キーの状態を1つの report で送る。`enableNkro()` を begin 前に呼んでいないと false。Host が boot protocol を選んでいる間は、6キーの boot 形式に変換して送る。
- `pressUsage` / `releaseUsage` / `tapUsage` / `pressKey` / `tapKey` / `write(text)` / `releaseAll`：usage ID や文字で操作する。
- `ledState()`：Host から届いた LED 状態（Num/Caps/Scroll Lock など）。callback を登録していなくても更新され、bus reset や抜去で消える。
- `onOutputReport(callback)` / `onProtocol(callback)` / `enableNkro()`：停止中だけ `ESP_OK`。動作中は `ESP_ERR_INVALID_STATE` を返し、設定は変わらない。
- `protocol()`：現在の protocol（0＝boot、1＝report）。
- `heldState()`：NKRO で最後に Host へ伝えた状態。NKRO 無効のときは空のまま。

report 長は boot 形式 8 byte、NKRO 29 byte です。

## Mouse

`move` / `wheel` / `press` / `release` / `click` / `releaseAll` / `sendReport` / `sendState` で操作します。
report protocol では 4 byte（ボタン、X、Y、ホイール）、boot protocol では 3 byte に変換して送ります。
X/Y/ホイールのような相対値は、送信が成功したら内部の状態を 0 に戻します。idle 再送で同じ移動が繰り返されないようにするためです。

## Gamepad

`sendReport(EspUsbDeviceGamepadReport)` または `send(x, y, z, rz, rx, ry, hat, buttons)` で送ります。
hat（方向キー）は `ESP_USB_DEVICE_GAMEPAD_HAT_CENTER`（0＝中央、何も押していない）と、`HAT_UP`（1）から時計回りに `HAT_UP_LEFT`（8）までの定数で指定します。1〜8 は 0〜315 度に対応します。

## Consumer Control

`press` / `release` / `click` / `sendUsage` に Consumer ページの usage ID（16 bit）を渡します。
`ESP_USB_DEVICE_CONSUMER_CONTROL_VOLUME_UP` などの定数があります。

## System Controlのusage指定

`press` / `sendUsage` / `click` には Generic Desktop ページの標準 usage ID を渡します。`usage()` も同じ値を返します。

| 定数 | 値 |
| --- | --- |
| `ESP_USB_DEVICE_SYSTEM_CONTROL_POWER_OFF` | 0x81 |
| `ESP_USB_DEVICE_SYSTEM_CONTROL_STANDBY` | 0x82 |
| `ESP_USB_DEVICE_SYSTEM_CONTROL_WAKE_HOST` | 0x83 |
| （解放） | 0 |

```cpp
systemControl.press(ESP_USB_DEVICE_SYSTEM_CONTROL_STANDBY);
systemControl.release();
```

- それ以外の値は false で拒否し、保持している値、送信中の内容、GET_REPORT で返す内容は変えません。
- 定数名で呼んでいたコードはそのまま動きます。整数 1/2/3 を直接書いていたコードは、標準 usage か定数へ直してください。`usage()` の戻り値を数値で比べているコードも同様です。
- wire 上では従来どおり 1/2/3 の選択値（0＝何も押していない）を送り、descriptor でそれを 0x81/0x82/0x83 に対応させています。以前の descriptor は usage 0〜3 を広告していて、電源操作の意味になっていませんでした。
- `WAKE_HOST` は普通の HID 入力です。bus が休止しているときに [remote wakeup](V3_REMOTE_WAKEUP.ja.md) を自動で行うものではありません。
- この変更でメソッドの型、report の大きさ、descriptor 長、確保するメモリは増えていません。

根拠：[HID Usage Tables 1.7](https://usb.org/sites/default/files/hut1_7.pdf) の Generic Desktop の表と §4.5.1、[HID 1.11](https://www.usb.org/sites/default/files/hid1_11.pdf) §6.2.2.5 の array の推奨（logical minimum＝1、0＝未押下）。

## Vendor（生データ）

| 関数 | 戻り値・制約 |
| --- | --- |
| `sendInput(data, length, timeoutMs)` | 送信を始めたら true。`length` が 0 または `reportSize()` を超えると false |
| `onOutputReport(callback)` / `onFeatureReport(callback)` | 停止中だけ `ESP_OK`、動作中は `ESP_ERR_INVALID_STATE` |
| `reportSize()` | constructor で指定した payload 長 |
| `maxReportSize()` | 65534。内部の転送長が 16 bit で、report ID の 1 byte を除いた値 |

`maxReportSize()` は確保が成功することを保証する値ではありません。使えるメモリ量と Host 側の対応も制約になります。`reportSize` が上限を超えると begin が `ESP_ERR_INVALID_SIZE` で失敗します。

## Feature値の更新

```cpp
uint8_t value[] = {1, 2, 3};
esp_err_t error = pipe.setFeatureReport(value, sizeof(value));
```

Feature report の現在値を設定します。Host への interrupt 送信や `onFeatureReport` の通知は起きず、次に Host が GET_REPORT したときに返ります。

- begin 成功後に使えます。Host が接続する前に設定した値も、列挙時の reset で消えません。
- payload に report ID は含めません。戻った後は元の配列を再利用できます。
- `reportId` を省略（0）すると、複合構成ではその Function に割り当てた ID、単独なら番号なしまたは唯一の Feature ID を選びます。Feature ID が複数ある Custom HID では ID を明示します。
- アプリ task からも USB worker からも呼べ、読み出しと同時に呼んでも値が混ざりません。

| 戻り値 | 条件 |
| --- | --- |
| `ESP_OK` | 設定した |
| `ESP_ERR_INVALID_ARG` | `data` が null か `length` が 0、別 Function の ID を指定、ID が一意に決まらない |
| `ESP_ERR_INVALID_STATE` | 開始前、または停止処理中 |
| `ESP_ERR_NOT_SUPPORTED` | その ID に Feature report がない |
| `ESP_ERR_INVALID_SIZE` | 宣言した長さを超える |

## 独自HIDと容量

独自の HID は `EspUsbHidFunction` を基底クラスにします（既存の `EspUsbDeviceHidCustom` を使うだけなら変更は要りません）。

- `EspUsbHidFunction` は `EspUsbDeviceClass` を継承し、begin/end、転送、確保エラー、停止を共通で実装します。独自の `begin()` では検証の後に基底の `begin()` を呼び、その結果を返します。
- 2.x で `EspUsbDeviceClass` から直接 HID を作っていた拡張は、この基底へ移す必要があります。
- 独自 begin が途中で失敗しても、Device が end を呼んで基底の状態を元に戻します。登録していない、または開始済みの Function の begin は `ESP_ERR_INVALID_STATE` になります。動作中に Function の end を呼ぶと Device 全体が止まります。
- 派生クラスが callback や応答データを持つ場合は、派生 destructor の先頭で `end()` を呼び、worker が止まってからメンバーを破棄してください。標準の HID クラスはそうなっています。
- `EspUsbDeviceHidCustom` に渡す report descriptor の配列は、動作中も有効で変更しないまま保ってください。

### 容量の照会

`inputReportCapacity()` / `outputReportCapacity()` / `featureReportCapacity()` は、共有 interface に必要な最大 byte 数を返します。report ID を宣言した report は ID の 1 byte も含みます。開始前と終了後は 0 です。
上の例（`reportSize` 257）なら、Input/Output/Feature とも report ID 込みで 258 byte です。

### descriptor の解析

begin 時に report descriptor を解析し、Report Size / Count / ID と global の Push/Pop を見て、ID と種類（Input/Output/Feature）ごとの bit 数を合計します。
次のものは begin で拒否します：途中で切れた item、Push/Pop の不整合、Report ID 0 の明示指定、番号付きと番号なしの report の混在、転送長の上限超過。
これは HID の usage や意味を全部検査する仕組みではありません。

### 確保する領域

| 領域 | 確保する条件と大きさ |
| --- | --- |
| 共有の状態・report 表 | HID を開始するとき1回。使う ID の数だけ |
| IN の DMA 領域 | Input report があるときだけ。最大 Input report 長 |
| OUT の DMA packet 領域 | Output report と interrupt OUT endpoint があるときだけ。選んだ速度の最大 packet 長 |
| OUT report の組み立て領域 | 同上。最大 Output report 長 |
| 状態の保存領域 | 定義した ID/種類ごとの report 長の合計。GET_REPORT と idle 再送で使う |
| control 応答用の領域 | 定義した Input/Output/Feature の最大長 |
| 相対値の mask | 相対値の Input 項目がある report だけ。保存領域と一緒に確保 |

- Input だけの HID では OUT 側と Feature の保存領域を確保しません。GET_REPORT 用の Input 保存と control 応答用の領域は必要です。
- 転送中に heap は使いません。HID を使わないプログラムには、これらの状態・driver・buffer はリンクされません。
- 確保に失敗すると begin が `ESP_ERR_NO_MEM` で失敗し、確保済みの領域と driver 登録をすべて戻します。
- end は USB worker と DMA を止めてから解放します。

## controlと受信

### Host からの要求

独自 driver で次を処理します：HID descriptor と report descriptor の照会、Input/Output/Feature の GET_REPORT、Output/Feature の SET_REPORT、report ID ごとの GET_IDLE/SET_IDLE、boot interface の GET_PROTOCOL/SET_PROTOCOL。bus reset で report protocol に戻ります。

- SET_REPORT は、方向・宛先・interface、ID と種類、長さ、実際に受けたデータ量を検査し、ID 付き形式で先頭の ID が合わないものは成功を返す前に拒否します。
- control 経由の番号付き report は、「ID 付きの全長」と「wValue だけで ID を指定する payload」の両方の形を受け付けます。
- 知らない ID/種類や、方向・interface が合わない要求には STALL を返します。

### interrupt OUT の受信

- 1 packet ずつ受け、ID ごとの宣言長に達するか、短い packet が来た時点で1つの report とします。最大 report より短い別 ID の report がちょうど packet 長の倍数でも、余分な長さ0の packet（ZLP）は要りません。
- callback へは ID を分けた payload と長さを渡します。データはその callback の間だけ有効です。
- callback の中で end、Function の破棄、設定変更をしないでください。

### GET_REPORT で返す値

- ID と種類ごとに保存した状態を返します。初期値は ID と 0 埋めの payload です。
- Input は送信を受け付けたとき、Output/Feature は SET_REPORT が完了したときに更新します。
- 短い payload を送ると、保存する長さも短くなります。黙って 0 埋めはしません。
- 応答の開始時に専用領域へコピーするので、応答の途中でアプリが値を変えても1つの応答の中で混ざりません。
- Input の相対値（Mouse の X/Y/ホイールなど）は送信成功後に 0 へ戻し、絶対値は保持します。descriptor の bit 位置から mask を作っています。

### protocol の切り替え（boot ⇔ report）

- Keyboard は boot 8 byte ⇔ NKRO 29 byte、Mouse は boot 3 byte ⇔ report 4 byte に切り替わります。切り替えると保存している Input は中立の値に戻ります。
- SET_PROTOCOL で形式が変わるときは、送信中の古い IN をハードウェアで止めてから応答します。古い完了通知を core の FIFO fence（それより前の通知をすべて処理し終えるまで待つ仕組み）で流し終えるまで、明示送信と idle 再送の両方を保留します。
- 固定時間の delay や、endpoint の STALL/clear-halt で DATA PID（DATA0/DATA1 の交互の印）を初期化する方法は使いません。
- boot に対応した番号なし Input は、現在の形式の正確な長さを求めます。たとえば NKRO が有効な report protocol へ 8 byte の boot 配列を直接送ると `ESP_ERR_INVALID_SIZE` で拒否されます。通常の Keyboard の関数は形式を自動で選びます。形式を選んでから送るまでに Host が切り替えた場合は、合わない packet を送らずに拒否するので、アプリ側で再試行できます。
- 独自の boot 対応 descriptor は、番号なし report で必要な長さを持つものだけを受け付けます。短すぎる boot descriptor は begin で拒否します。
- protocol 切り替えのために heap や専用 task を追加で確保することはありません。

## idleと設定変更

Host が SET_IDLE で指定した値×4 ms ごとに、その Input report の保存状態を自動で再送します。

- 0 は自動再送なし（既定）、ID 0 はすべての Input ID への指定です。GET_IDLE は ID ごとの設定値を返します。
- 期限は前回の転送完了から数えます。IN が使用中なら完了後に送ります。複数の ID があるときは期限の古い方を先に送ります。
- 期限直前に周期を変えると、次の report の後から反映されます。送信済みの IN は、idle を 0 にしても完了まで保持します。
- 再送は Host が有効にしている間だけ worker に再投入し、タイマーやメッセージ用の heap は追加しません。
- reset と end で止まります。suspend 中は止まり、resume 後に再開します。

bus reset と unconfigure（SET_CONFIGURATION 0）では Input/Output を中立の値に戻し、idle を無効にします。Feature は Device の設定値として保持し、end か次の begin で初期化します。

callback 登録関数（Keyboard の `onOutputReport` / `onProtocol` / `enableNkro`、Vendor の `onOutputReport` / `onFeatureReport`）は、CDC と同じく `esp_err_t` を返し、停止中だけ設定できます。戻り値を無視する書き方はそのまま使えます。戻り値が void のメンバー関数ポインタを使っていた拡張は型の変更が必要です。
`protocol()` は atomic な値の snapshot を返すので、worker からの更新と競合しません。

## 標準interface/endpoint要求による回復

Host が送る標準要求で、HID の転送状態を初期化します。

- **SET_INTERFACE（alt 0）**：その HID interface の IN/OUT の DMA を止め、古い完了通知を fence で流してから、送信中の印と受信途中の組み立て状態を捨て、両 endpoint を DATA0 に戻します。
- **SET_FEATURE(ENDPOINT_HALT)**：指定した方向を止めます。**CLEAR_FEATURE** でその方向を DATA0 に戻して再開します。もう一方の endpoint の転送と DATA PID はそのままです。OUT は内部で受信を再開します。

注意点：

- 送信関数はコピーを受け付けた時点で true を返すので、受け付け済みでも未完了の report はこれらの初期化で捨てられることがあります。確実な配送確認が必要なら、Host とアプリの間で確認・再送を行ってください。
- HALT 中と fence 待ちの間は新しい IN を受け付けません。
- 受信途中の OUT を新しい report につなげることはなく、未完成の report で callback を呼ぶこともありません。
- GET_REPORT 用の保存値、protocol、idle 設定は保持します。idle が有効なら通常の再送を再開します。
- これらは bus reset とは違い、protocol を report に戻しません。SET_PROTOCOL による形式変更では、従来どおり IN だけを止めて DATA PID を保ちます。
- 専用 task や転送時の heap 確保は追加していません。既存の idle 用 job を方向別の fence 予約にも使います。

## 複合HIDのdescriptorの扱い

複数の HID Function をまとめた descriptor では、各 Function の report descriptor 全体を global の Push/Pop で囲みます。ある Function の単位や物理範囲が、次の Function へ引き継がれないようにするためです。

- Function の中の Push/Pop はそのまま有効です。
- まとめた descriptor は Function ごとに 2 byte 長くなります。begin 時の解析では、実際の Push の深さだけ一時的な stack を使い、解析後に解放します。
- 単独の Function ではこの処理をしません。

## FSとHSの違い

| 項目 | FS（S3 など） | HS（P4/P4X の HS controller） |
| --- | --- | --- |
| interrupt endpoint の最大 packet 長 | 64 byte | 1024 byte |
| HidVendor の packet 長 | `reportSize + 1` と 64 の小さい方（大きい report は複数 packet） | `reportSize + 1` が 64 を超えると、その値（最大 1024） |
| 複合 HID の packet 長 | 各 Function の希望の最大。64 を超えると begin が `ESP_ERR_INVALID_SIZE` | 同。1024 を超えると `ESP_ERR_INVALID_SIZE` |
| 非 control IN endpoint の数の上限（全 Function 合計） | S3 は 4 | P4 の HS は 7 |

HS で 512 byte を超える interrupt OUT packet を使う場合、Host 側の周期転送用 OUT FIFO が既定（512 byte）では足りないことがあります。
EspUsbHost を Host に使う試験では、P4 で `config.fifo = ESP_USB_HOST_FIFO_LARGE_PERIODIC_OUT` を指定しています（`validation/s3/hid_transport`）。

## 2.x からの移行

- `CFG_TUD_HID_EP_BUFSIZE`、旧 `CFG_TUD_HID_BUFSIZE`、`CFG_TUD_HID`（0 を含む）の指定は廃止しました。残っているとビルドが移行を促すエラーになります。
- report の大きさと packet の大きさを同じビルド定数で決める仕組みはなくなりました。
- 通常の Keyboard/Mouse の送信コードは変更不要です。
- 変更が必要なもの：`EspUsbDeviceClass` から直接作った独自 HID、System Control の数値直書き、`HidVendor::sendInput()` に長すぎる payload を渡していたコード、callback 登録関数の戻り値型に依存するコード。
- Gamepad の hat 定数は変わっていません。

## 設計判断の理由

- **runtime 確保にした理由**：ビルド設定なしで大きな report を使える、不要な OUT/Feature 資源を省ける、HID を使わないときは HID class 全体がリンクされない、構成ごとの確保量と失敗理由が明確になる。代わりに、begin 時の解析と確保、独自 HID 基底への移行、不正な descriptor や長すぎる送信の明示的な拒否が増えた。
- **GET_REPORT の保存領域**：状態の保存と応答用のコピーのため、定義した report の長さ分と control 応答用の RAM が追加で要る。
- **Gamepad の hat**：以前は logical range が 0〜8 で、中央 0 が方向として扱われ、上（1）や右（3）も約 39/118 度に対応していた。logical range を 1〜8 にし、Input を Null 状態対応にして、中央 0 を「入力なし」にした。hat の後の buttons も物理範囲 0〜1 に戻した（Physical Maximum の指定で descriptor が 2 byte 増えるが、report の大きさや member は増えない）。8 bit の wire 値は変えていない。根拠は [HID 1.11](https://www.usb.org/sites/default/files/hid1_11.pdf) §6.2.2.5 の hat と null の説明。
- **System Control**：Consumer Control と同じく API で usage を指定できるようにした。array の Input では Data/Array/Absolute 以外の属性を意味の根拠にしない。

## 実測の記録

S3 同士（FS）の結果です。HS 直結の結果は最後にまとめました。

### 転送と容量（2026-10-26）

- 初期3試験が成功（139.25 秒）。`hid_transport` の個別試験も成功（95.25 秒）。
- payload 1/63/64/65/127/128/129/255/256/257/511/1023 byte の IN、interrupt OUT、control の Output（ID あり/なし）と Feature、無効な要求の後の復帰、idle 値、boot/NKRO の切り替えと LED を確認。
- descriptor だけの確認で、最大 65535 byte の領域確保と、繰り返しでの heap 回復を確認。最大長の wire 転送そのものは行っていない。Host 側の probe は複数 packet を明示的に連結して照合しており、任意の OS アプリの read の挙動を示すものではない。
- 標準の `--combo all`（`runtime-hid-all.xml`）は9件成功、474.47 秒。176 箇所の確保失敗と回復を含み、全件で両ボード STOPPED、panic なし。
- 終了順序と解析 stack の故障注入を加えた `hid-transport-final.xml` は2件成功、91.43 秒。動作中の Vendor HID/Keyboard の破棄、callback を有効にしたままの停止、194 箇所の失敗と回復を確認。
- 開始成功時の `startError() == ESP_OK` を `hid-transport-errors.xml`（1件、53.04 秒）、未登録 HID の begin 拒否と既存7 HID の通信継続を `hid-registration-final.xml`（1件、44.01 秒）で確認。

### GET_REPORT と idle

- `hid-report-state.xml` は2件成功、127.44 秒。2 ID の Input/Output/Feature、初期値、Feature の reset 越しの保持、並行更新中の200回の読み出しの整合を確認。
- idle 20/100 ms の指定は、この S3 FS 接続で平均 20.873/108.002 ms だった。大きい report の複数 packet 転送時間を含む観測値で、精度保証や性能測定ではない。ID 単位と全体の再送停止も確認。
- 相対値の mask と Mouse の boot/report 形式を加えた `hid-state-relative.xml` は3件成功、123.33 秒。200 箇所の失敗と回復、Feature 更新で追加確保がないこと、相対値を 0 に戻した idle 再送を確認。最大長の descriptor だけの確認は、保存領域を考慮して Input だけの 65535 byte 構成で行った。
- 最終の `--combo all`（`hid-report-idle-all.xml`）は10件成功、520.13 秒。200 箇所の失敗と回復、Feature 設定で追加確保なし、短い boot 定義の拒否、停止中だけ設定できることを含む。全件で両ボード STOPPED、panic なし。最小構成3種の nm で、HID 未使用時に driver・保存領域・idle 処理がリンクされないことを確認。

### protocol 切り替え（`validation/s3/hid_protocol`）

- 公開の IDF Host API で IN を手動で投入し、古い report を Device の DMA に保留させた状態で、Keyboard 29⇔8 byte、Mouse 4⇔3 byte を各4回切り替えた。2つ目の送信の拒否、切り替え後 30 ms は受信しないこと、新しい印の全 byte と長さ、誤った形式の `ESP_ERR_INVALID_SIZE` を確認。
- 初回は Core で有効な列挙 filter の callback を省いていたため Host が準備できず失敗した。標準の `enum_filter_cb` で対象 VID/PID を許可して直した。この試験だけ manual Host を使い、Host library の patch や pytest の独自 fixture は追加していない。
- `hid-protocol-related.xml` は4件成功、171.86 秒（313 箇所の失敗と回復を含む）。全件で両ボード STOPPED、panic なし。記録は `.pytest-results/hid-protocol-related*`。
- この時点の `--combo all` の対象は12件だったが、実行したのは関連4件だけ。

### 標準要求による回復（2026-10-27、`validation/s3/hid_standard`）

- 129 byte の report の部分的な IN/OUT を3回初期化し、未完成の OUT が通知されないことと、その後の通信を確認。HALT ありなしの CLEAR_FEATURE を両方向で行い、反対方向が DATA1 のまま残り 65 byte を続けることを確認。両方向とも部分転送後に HALT/解除した条件では Host の pipe だけを作り直し、余分な SET_INTERFACE で回復を隠していない。
- `hid-standard-related.xml`（hid_standard、hid_protocol、hid_transport、hid_report_state）は4件成功、636.88 秒。8つの serial log すべて STOPPED、panic なし。記録は `validation/.pytest-results/hid-standard-related*`。
- Vendor コピー方式の最小構成の nm で、HID 本体と方向別の停止処理が含まれないことを確認（`hid-standard-vendor-copy-build.txt`、`hid-standard-vendor-copy-nm.txt`）。
- 試験 plugin は 1.10.0（commit `698bb29`）。

### bus 休止中の idle と再開（`hid_report_state` の拡張）

- Host から実際の root port を3回休止・再開した。wire 長 8/130 byte の2 ID に idle 値 5/25（20/100 ms）を設定し、休止中 150 ms の間 Host の受信数が変わらないことと、Device の明示 Input が拒否されることを確認。
- 再開後は 350 ms の間に短い report を18件、大きい report を4件受信し、GET_IDLE と GET_REPORT の値も保たれた。再送停止後の静止、200回の Feature 読み出しの整合、最後の bus reset 後の状態も確認。
- 製品 `512edb6` のまま1件成功、80.33 秒。STOP_FAILED/panic なし。記録は `.pytest-results/hid-suspend*`。
- Host 起点の休止の結果で、remote wakeup や OS の省電力の確認ではない。

### HS 直結（2026-10-31）

- S3 の peer 試験（`hid_transport` などを含む）を P4/P4X の HS 直結でも収集した。HS では interrupt OUT の packet が 1024 byte に達するため、Host 側（P4）の周期 OUT FIFO を大きくして実行した。結果は [P4/P4X と追加試験環境](V3_P4_VARIANTS_AND_BENCH.ja.md#s3-peer試験をhs直結へ広げた結果2026-10-31) を参照。
- `validation/peer/cdc_hid` は CDC と boot keyboard の複合を HS で確認する probe。

### 未確認の範囲

すべての OS、S2/S31、休止中の protocol 変更や切断などの全組み合わせ、idle 期限付近の細かい精度、未完了の IN を伴う protocol 変更の競合、SET_INTERFACE と idle の同時条件、人工的な古い完了通知・queue 満杯・reset/end の全順序。

規格：[USB HID 1.11](https://www.usb.org/sites/default/files/documents/hid1_11.pdf)
