# EspUsbDevice Keyboard (NKRO)

> English: [README.md](README.md)

N-key rollover（NKRO）対応の USB HID keyboard を作る例です。boot report の
6キー制限と違い、任意の数のキーを同時に押下状態にできます。

## ハードウェア

- USB device 対応の ESP32-S3 など Arduino-ESP32 board
- PC、EspUsbHost を動かす別 ESP32、またはテスト治具などの USB host
- ログ確認用の Serial monitor 接続

## 動作内容

- `device.begin()` の前に `keyboard.enableNkro()` で NKRO を有効化します。
- 10キー + Left Shift を `EspUsbDeviceNkroKeyboardReport` に組み、状態全体を
  **1レポート**で送ります（6キー boot report では不可能）。その後まとめて解放します。
- `keyboard.heldState()` を読み、Host へ最後に伝えた状態を表示します。
- Host が boot protocol（BIOS）を要求した場合は自動で6キー形式に畳んで応答します。

## この例での NKRO の仕組み

- report protocol では bitmap レポートを送ります。modifier 1バイト + usages
  `0x00`-`0xDF` をカバーする 224bit のキービットマップで、各キーが専用ビットを
  持つためロールオーバー制限がありません。
- bitmap 範囲は International1-9（`0x87`-`0x8F`）と LANG1-9（`0x90`-`0x98`）を
  含むので、JIS など US 以外のレイアウト固有キーも通ります。
- Host が boot protocol（BIOS/UEFI）を選んだ場合は、押下キーを先頭6個に畳んで
  標準の6キー boot report を送るため、OS の HID ドライバ読み込み前でも動作します。
- ~29バイトの bitmap レポートを1回の転送で送れるよう、IN エンドポイントの
  packet size を 32バイトに引き上げています（`CFG_TUD_HID_EP_BUFSIZE` = 64 以内）。

## 主要 API

- `keyboard.enableNkro()` で NKRO を有効化します。`device.begin()` の前に呼びます。
- `keyboard.nkroEnabled()` は NKRO が有効かを返します。
- `keyboard.sendReport(nkroReport)` は保持キー全体を1レポートで送ります。複数キーが
  同時に変化する場合、あるいは毎周期にキー集合全体を計算する設計では、こちらを使います
  （下の増分 API は1キーの変化ごとに1レポートになるため、Host には chord が1キーずつ
  届きます）。
- `EspUsbDeviceNkroKeyboardReport` は `modifiers` と28バイトの `bitmap` を持ち、
  `press(usage)` / `release(usage)` / `isDown(usage)` / `clear()` で操作します。
  modifier usage `0xE0`-`0xE7`（Left Shift は `0xE1`）は自動的に `modifiers` へ入ります。
  `press()` が false を返すのは、このレポートで表現できない usage（`0xDF` 超で
  modifier でもないもの）のときだけです。
- `keyboard.heldState()` は Host へ最後に伝えた状態を返します。状態が変わっていない
  ときの送信を省く（ライブラリ側では抑制しません）、`releaseAll()` によらず再同期する、
  といった用途に使えます。
- `keyboard.pressUsage(usage, modifiers)` / `keyboard.releaseUsage(usage)` は
  個別キーを押下・解放します。NKRO では任意数を同時押下できます。
- `keyboard.releaseAll()` は押下中の全キーを解放します。
- `keyboard.write()` / `tapKey()` / `pressKey()` / `setLayout()` は 6KRO keyboard と
  同じく順次テキスト入力に使えます。

## 想定 Serial 出力

```text
USB NKRO keyboard ready (nkro=1)
after releaseAll: A still down? no
sent 10-key chord (protocol=report)
after releaseAll: A still down? no
sent 10-key chord (protocol=report)
```

## 関連

- [Keyboard](../Keyboard/) - 標準の6キー boot keyboard
- [KeyboardMouse](../KeyboardMouse/) - keyboard と mouse の composite device

## 読み方と確認の手順

### 実行前の準備

保持キーのbitmapと明示的な解放を送る。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

`begin()`前にNKROを有効にし、`EspUsbDeviceNkroKeyboardReportのpress()`とsendReport()を追います。key testerで同時押しを見て、`releaseAll()`で終わることを確認します。多キーにはホスト・reportの対応が必要で、boot protocolでは小さい形式になります。

### APIの使い方と改造時の注意

HID reportは構造化された状態や変化であり、任意のテキスト一行ではありません。report descriptorと合わせて、report ID・usage page/usage・符号・数値範囲を読みます。キーボードはキー位置、マウス移動は相対値、ボタンはbitmapです。実際の操作へつなぐ前に、押下と解放の対、または全状態の比較を確認します。

callbackのreportは借りたデータです。別taskへ渡す場合はbyte列や必要な値を自分の領域へコピーします。callbackでは短い診断に留め、生dumpの連続表示で端末速度が律速になることにも注意します。入力が来なければ、列挙、descriptor・report長、アプリ側の解釈の順に調べ、一度に全部を変えないようにします。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](KeyboardNKRO.ino)
- [全サンプル](../README.ja.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
