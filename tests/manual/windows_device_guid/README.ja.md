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

**対照こそがテストです。** これが無いと、B が更新されたことは「Windows が何かを読み直した」
としか言えず、「revision が読み直させた」のか「毎回読み直している」のかを区別できません。
C は revision を据え置いたまま別の GUID を送り、Windows は古い方を保持しました。仕様どおりに
機構が働いているということです。C′ は、詰んだ状態にならないことを示します——revision を
自動導出に戻せば新しい GUID が入ります。
