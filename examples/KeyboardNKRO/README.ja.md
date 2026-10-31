# EspUsbDevice Keyboard（NKRO）

> English: [README.md](README.md)

N キー同時押し（NKRO）の USB HID keyboard を作る例です。boot report の6キーの制限がなく、
いくつでも同時に押したままにできます。

## ハードウェア

- ESP32-S3、ESP32-S2、ESP32-P4 の board。native USB OTG の口を Host へつなぐ
- PC、EspUsbHost を動かす別の ESP32、試験治具などの USB Host
- `Serial`（115200）用の別の debug 接続。入力とログの両方に使う

## 動作内容

- `device.begin()` の前に `keyboard.enableNkro()` で NKRO を有効にし、戻り値を確かめます。
- debug port で `c` を受けると、10キーと Left Shift を `EspUsbDeviceNkroKeyboardReport` に入れ、
  全体を **1つの report** で送ります（6キーの boot report ではできない）。その後まとめて離します。
- `keyboard.heldState()` を読み、最後に Host へ伝えた状態を表示します。
- `keyboard.onProtocol()` で boot/report protocol の切り替えを表示します。
- boot protocol（BIOS）には自動で6キーの形式で応えます。

`c` を送る前に Host でキーテスターを開いてください。それまでは何も送りません。

## NKRO の仕組み

- report protocol では bitmap 形式の report を送ります。modifier 1 byte と、usage `0x00`〜`0xDF` の
  224 bit の bitmap（計 29 byte）です。キーごとに bit があるので同時押しの制限がありません。
- bitmap は International1〜9（`0x87`〜`0x8F`）と LANG1〜9（`0x90`〜`0x98`）も含むので、JIS など US 以外のキーも使えます。
- Host が boot protocol を選ぶと（主に BIOS/UEFI）、押しているキーのうち最初の6つを 8 byte の boot report にして送ります。
  OS の HID driver が読み込まれる前でも使えます。
- interrupt IN の packet は 32 byte で、29 byte の report が1 packet に収まります。
  ビルド設定は関係なく、report の領域は `begin()` で確保します。

## 主要 API

- `keyboard.enableNkro()` は NKRO を有効にします。`device.begin()` の前に呼びます。開始後は `ESP_ERR_INVALID_STATE` です。
- `keyboard.nkroEnabled()` は NKRO が有効かを返します。
- `keyboard.sendReport(nkroReport)` は押しているキーの全状態を1つの report で送ります。
  複数のキーが同時に変わるときや、毎回キーの全体を計算するときに使います。下の1キーずつの API は
  キーが変わるたびに report を送るので、Host には和音が1キーずつ届くように見えます。
- `EspUsbDeviceNkroKeyboardReport` は `modifiers` と 28 byte の `bitmap` を持ち、`press(usage)` /
  `release(usage)` / `isDown(usage)` / `clear()` で操作します。modifier の usage `0xE0`〜`0xE7`
  （Left Shift は `0xE1`）は自動で `modifiers` へ入ります。`press()` が false を返すのは、
  modifier でない `0xDF` より大きい usage（この report で表せない）のときだけです。
- `keyboard.heldState()` は最後に Host へ伝えた状態を返します。同じ状態の再送を省く
  （library は省かない）ときや、`releaseAll()` を使わずに同期を取り直すときに使います。
- `keyboard.pressUsage(usage, modifiers)` / `keyboard.releaseUsage(usage)` はキーを1つずつ押し・離します。NKRO ではいくつでも同時に押せます。bitmap で表せない usage（modifier でない `0xDF` より大きいもの）は `ESP_ERR_INVALID_ARG` で失敗します。
- `keyboard.releaseAll()` はすべてのキーを離します。
- `keyboard.protocol()` は 0（boot）か 1（report）を返します。`keyboard.onProtocol()` は切り替えを USB worker で通知し、`begin()` の前に登録します。
- `keyboard.write()`、`tapKey()`、`pressKey()`、`releaseKey()`、`setLayout()` は、順に文字を入力する用途では6キーの keyboard と同じです。NKRO を有効にすると、これらも bitmap の report で送ります。

## 想定 Serial 出力

```text
USB NKRO keyboard ready (nkro=1)
Focus a key tester on the Host, then send c on this port
after releaseAll: A still down? no
sent 10-key chord (protocol=report)
```

`PROTOCOL boot` / `PROTOCOL report` は Host が SET_PROTOCOL を送ったときだけ出ます。

## 確認の手順

- キーテスターで和音を見て、`releaseAll()` で終わることを確かめます。
- report はキーの全状態です。アプリの動作につなぐ前に、全状態で比べるか、押下と解放を対にします。
- 7キー以上には report protocol と、NKRO の descriptor を読める Host が要ります。boot protocol では6キーの形式のままです。
- `sendReport()` の true は「コピーして送信を始めた」という意味で、Host が読んだことは意味しません。

## v2 からの変更点

- 和音は4秒ごとではなく、debug port で `c` を送ったときに送ります。
- `keyboard.enableNkro()` と `keyboard.onProtocol()` の戻り値が `esp_err_t` になりました（`begin()` 前は `ESP_OK`、後は `ESP_ERR_INVALID_STATE`）。この例は戻り値を確かめます。
- 32 byte の IN packet は `CFG_TUD_HID_EP_BUFSIZE` に依存しなくなりました。この設定は廃止され、定義するとビルドエラーになります。
- `sendReport(EspUsbDeviceNkroKeyboardReport)`、`heldState()`、`protocol()`、`releaseAll()` は変わりません。

## 関連

- [Keyboard](../Keyboard/) - 6キーの boot keyboard
- [KeyboardMouse](../KeyboardMouse/) - keyboard と mouse の複合
- [HID API ガイド](../../docs/classes/hid.ja.md)
- [全サンプル](../README.md)
