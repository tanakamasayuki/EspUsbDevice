# windows_device_guid

> English: [README.md](README.md)

**指定した GUID を Windows が本当に記録するか。そして変更したとき、すでにそのデバイスを
見たことがある PC で反映されるか。**

重要なのは 2 つめです。Windows は VID/PID/serial ごとに、最初に列挙したときの registry
property をキャッシュし、`MS_OS_20_FEATURE_VENDOR_REVISION` が変わったときだけ読み直します。
この descriptor を出さないライブラリは、新しい `DeviceInterfaceGUIDs` をいくら publish しても、
一度そのデバイスに会った PC には永久に届きません。EspUsbDevice 2.4.0 がまさにその状態でした。

自動テストでは答えられません。`tests/single/descriptor` が保証するのは「送っている byte が
正しいこと」で、「Windows が何を保持したか」を言えるのは Windows だけです。

## 必要なもの

- native USB が Windows PC に繋がった ESP32-S3。**WSL に attach していないこと**
  （`usbipd.exe detach --busid <n>`）。attach 中は Windows 側からは USBIP Shared Device に
  見え、ドライバは何も当たりません
- 書き込み用の別系統 UART（native USB は試験対象そのものなので）

## 実行

identity は全変種で固定（`303a:4080`、serial `guid-test-1`）で、動かすのは GUID と revision
だけです。これが設計の要点で、identity を変えると Windows は「別のデバイス」として
descriptor を読み直すため、何も証明できません。

```sh
cd tests/manual/windows_device_guid
# A: GUID A、revision は自動導出
rm -f build_opt.h
arduino-cli compile --profile esp32s3 --clean . && arduino-cli upload --profile esp32s3 --port /dev/ttyACM3 .
cd ../../ && uv run python manual/windows_device_guid/windows_device_guid.py
```

以降、`-DGUID_VARIANT=1`（GUID B）、`-DGUID_VARIANT=2 -DPINNED_REVISION=<B の revision>`
（対照）、`-DGUID_VARIANT=2` 単独、の順で繰り返します。revision は起動時にスケッチが表示するので、
対照で使う数値はそこから取ります。**毎回 `--clean` を付けること。** `build_opt.h` は応答ファイル
経由でスケッチには届きますが、stale build ではライブラリが再コンパイルされません。

## 期待される結果

Windows 11 での実測。instance は全変種を通じて `USB\VID_303A&PID_4080\GUID-TEST-1`、
すべて `STATUS OK problem=CM_PROB_NONE` / `SERVICE WINUSB`。

| 変種 | 送った GUID | revision | Windows が保持した GUID |
|---|---|---|---|
| A | `{A1A1…}` | 21192（自動） | `{A1A1…}` |
| B | `{B2B2…}` | 563（自動） | `{B2B2…}` — 更新された |
| **C（対照）** | `{C3C3…}` | **563 に固定** | **`{B2B2…}` — 更新されない** |
| C′ | `{C3C3…}` | 12898（自動） | `{C3C3…}` — 更新された |

### 行列の残り

同じスケッチで identity と構成の軸も測れます。ユーザーガイドの「Windows が何を読み直すか」の
表はここから作りました。`-DVAR_PID=`、`-DVAR_SERIAL=`、`-DVAR_NO_SERIAL=1`、
`-DVAR_COMPOSITE=1`（HID + vendor）、`=2`（vendor + MSC）、`=3`（MSC を先に登録してから
vendor）で選びます。`-DVAR_HID_FIRST=1` は変種 1 で HID クラスを vendor クラスより先に
登録します。実測、すべて `STATUS OK` でドライバが当たった状態:

| 変更（特記なければ revision 固定） | instance | 結果 |
|---|---|---|
| vendor 単独 -> vendor + HID | 親は同じ、`&MI_00` / `&MI_01` が新規 | 親は `usbccgp` に再バインド、子が作られ、`MI_01` に `WINUSB` が当たり GUID を新規に読んだ |
| vendor + HID -> vendor 単独 | 親は同じ | **親が `usbccgp` -> `WINUSB` に再バインド**、GUID は保持（revision 不変） |
| `&MI_01` 子の GUID を変更 | 同じ子 | GUID 保持 — キャッシュは子にも効く |
| 公開版 2.4.0 でビルド、次に修正版でビルド | 同じ | revision descriptor が初めて現れ、新しい GUID が**入る** |
| `pid` 0x4080 -> 0x4083 | **新規** | 全部読み直し。revision は未使用値に固定 |
| `serialNumber` 変更 | **新規** | 全部読み直し |
| `serialNumber` なし | `…\8&2EBC545B&0&4` | serial ではなくポートでキーされる |
| 単一 interface（GUID A）-> 複合（GUID B）、新規 PID | 親は保持、子は新規 | 親は device スコープに A を持ち続け、子が B を持つ |
| `MI_00` / `MI_01` の function を入れ替え、数は同じ | **両方の子が同じ instance** | 両方再バインド: `HidUsb`->`WINUSB`、`WINUSB`->`USBSTOR`。GUID は vendor function を得た子に**追加**され、失った子には**残った** |
| 単一 vendor interface に `msOs20CcgpDevice` | 親 + 子 1 つ | 親に `usbccgp`、子 `&MI_00` に `WINUSB`、GUID は**子にだけ**入る — device スコープの値は書かれない |
| `SetupDiGetClassDevs(DIGCF_PRESENT \| DIGCF_DEVICEINTERFACE)` で列挙、**親**に残骸あり | - | **残骸の GUID は何も返さず、生きている方は interface をちょうど 1 つ返す** |
| 同上、**子**に残骸あり（`MI_01` は今 `USBSTOR`、生きている `MI_00` と同じ GUID） | - | **interface は `MI_00` の 1 つだけ** — mass storage 側の残骸も列挙されない |

最後の 2 行が残骸の重みを決めます。どちらも、それ以前の行が「残骸の値＝列挙されるデバイス」
であるかのように書き上げられた後で測りました。そうではありません。registry の値は device
interface を作らず、作るのはそのノードに当たったドライバです。親のケースを先に測り、子の
ケースは「同じ未検証の推論が子にもまだ残っている」と依頼側に指摘されてから測りました。

ユーザーガイドが拠り所にしている結論は 2 つ: **ドライバのバインドは毎回の列挙で descriptor に
従い、revision を要しない**。**revision の後ろにキャッシュされるのは Microsoft OS 2.0 の
registry property だけ**。

### ライブラリのバグだった行

上の HID + vendor ビルドはすべて、`MI_01` の vendor function に `WINUSB` が当たり、
`STATUS OK`、GUID も記録済み——なのに**列挙できませんでした**。`pnputil /enum-interfaces`
は interface を「無効」と表示し、`SetupDiGetClassDevs(DIGCF_PRESENT)` は何も返しません。
MSC の隣で `MI_00` にいる vendor は列挙できたので、interface 番号か HID 兄弟による Windows
側の挙動に見えました。どちらでもありませんでした:

| ビルド（毎回新規 PID） | HID 子 | vendor 子 | interface |
|---|---|---|---|
| HID + vendor、vendor を先に登録 | `HidUsb` **Code 10、到着から約 6 秒後** | その失敗の 2 ms 後に開始 | 登録済み、**無効** |
| MSC + vendor、vendor は `MI_01`（変種 3） | - | 到着から 27 ms で開始 | **有効** |
| HID + vendor、`-DVAR_HID_FIRST=1` | 到着から 21 ms で開始 | 到着から 33 ms で開始 | **有効** |
| HID + vendor、vendor を先に登録、**修正版ライブラリ** | 23 ms で開始 | 36 ms で開始 | **有効** |

時刻は `Microsoft-Windows-Kernel-PnP/Configuration` イベントログ（id 400/410/411）から
取りました。これは `Get-PnpDevice` と食い違います。`Get-PnpDevice` は両方の子に毎回
`STATUS OK problem=CM_PROB_NONE` を返しました——HID の開始がまだ保留中に取った回も、ログが
Code 10 を記録した 1 分後に取った回も含めて。子が健康に見えるのに動かないときは、status 列
ではなくイベントログを読んでください。失敗の後に開始した WinUSB の子がなぜ interface を
有効化しないままなのかは突き止めていません。到着後 20 秒から数分までのすべての確認で
interface は無効のままで、一度だけ、約 9 分後にそのポートが「不明な USB デバイス
（デバイス記述子要求の失敗）」になっていました。修正はこれらの状態の出発点である失敗を
取り除くので、状態ごと消えます。

原因はライブラリ側でした。HID クラスを TinyUSB のインスタンス番号で、登録テーブルの位置で
あるかのように引いていたため、vendor クラスの後に登録した HID クラスは report descriptor を
返さず、ホストの `GET_DESCRIPTOR(Report)` は応答も STALL もされませんでした。descriptor は
終始正しく——だからこそ、このスケッチが表示する descriptor dump は原因を指しませんでした。
失敗した HID 行と動いた HID 行の違いは登録順だけで、それは線上には見えません。いまは
`tests/single/hid_registration_order` と `tests/peer/composite_vendor_hid` が守っています。

**対照こそがテストです。** これが無いと、B が更新されたことは「Windows が何かを読み直した」
としか言えず、「revision が読み直させた」のか「毎回読み直している」のかを区別できません。
C は revision を据え置いたまま別の GUID を送り、Windows は古い方を保持しました。仕様どおりに
機構が働いているということです。C′ は、詰んだ状態にならないことを示します——revision を
自動導出に戻せば新しい GUID が入ります。
