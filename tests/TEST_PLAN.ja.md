# テスト計画

> English: [TEST_PLAN.md](TEST_PLAN.md)

## テスト方針

EspUsbDevice は、最終的には汎用的に使いやすい USB Device ライブラリにすることを
目的とします。テスト計画は `EspUsbHost` の peer / loopback シナリオから始めます。
これは低レベル API と descriptor を実ハードウェアで明確かつ再現性高く検証できるためです。

テストは、ソフトウェアで環境をどこまで制御できるかに基づいて分類します。

**自動テスト**は人の操作なしで実行します。入力はテストコードが生成し、期待値は
assertion で検証します。

**手動テスト**は、物理ハードウェア、ホスト OS 側の認識、配線、目視確認が検証の
本質に含まれる場合だけに使います。

### EspUsbHost とのテスト分担

`EspUsbHost` 側の peer テストは、原則として Arduino Core 標準 USB Device 実装を使った
現状の構成を維持します。これは Host 側が `EspUsbDevice` だけに過適応することを避け、
複数の USB Device 実装に対して動作を確認するためです。

`EspUsbDevice` 側では、released `EspUsbHost` を使って Host / Device 間の詳細テストを行います。
report descriptor、report ID、output / feature report、複合 HID、raw input callback など、
Arduino Core 標準 USB Device では制御しづらい項目をこのリポジトリの peer / loopback で検証します。

`EspUsbHost` の未リリース修正が必要な場合は、切り分け目的でローカルの Host checkout を
差し替えて任意確認してよいです。ただし通常の合格条件は released `EspUsbHost` を対象にします。
`EspUsbHost` がリリースされたら、このリポジトリ側の対応 Host バージョンを上げて詳細テストを
再実行し、互換性を確認します。

このため、peer / loopback の default profile は released `EspUsbHost` を使います。
通常の確認では default profile だけを使います。`s3_peer_local` と `p4_loopback_local` は、
Host 側の未リリース修正をこのリポジトリでリリース前検証する場合だけ使います。
例: `uv run --env-file .env pytest peer/ --profile=s3_peer_local`、
`uv run --env-file .env pytest loopback/ --profile=p4_loopback_local`。

```text
tests/
  unit/       自動 - descriptor builder と report helper。
  examples_compile/ 自動 - examples sketch の build-only smoke。
  peer/       自動 - EspUsbHost host + EspUsbDevice device の2台構成。
  loopback/   自動 - ESP32-P4 1台で host / device role を同時実行。
  probe/      初期切り分け - port / speed / PHY / OS認識の確認。
  manual/     手動 - 物理デバイスまたは人の判断が必要。
```

## カバレッジ計画

| 機能 | unit | peer | loopback | probe | manual |
|------|------|------|----------|-------|--------|
| device descriptor config | ✅ `descriptor` | | | 予定 | |
| FS/HS endpoint MPS | ✅ `descriptor` | 予定 | 予定 | 予定 | |
| HID keyboard raw report | ✅ `descriptor` | ✅ `hid_keyboard` | build済み `hid_keyboard` | | |
| HID keyboard LED output report | ✅ callback変換 | ✅ `hid_keyboard` | build済み `hid_keyboard` | | 任意 |
| HID mouse raw report | ✅ descriptor | ✅ `hid_mouse` | build済み `hid_mouse` | | |
| keyboard + mouse composite | ✅ descriptor | ✅ `hid_keyboard_mouse` | build済み `hid_keyboard_mouse` | | |
| custom HID report descriptor | 予定 | ✅ `custom_hid` | ✅ `custom_hid` | | |
| HID vendor IN/OUT/Feature | 予定 | ✅ `hid_vendor` | ✅ `hid_vendor` | | |
| consumer control HID | 予定 | ✅ `hid_consumer_control` | ✅ `hid_consumer_control` | | |
| system control HID | 予定 | ✅ `hid_system_control` | ✅ `hid_system_control` | | |
| gamepad HID | 予定 | ✅ `hid_gamepad` | ✅ `hid_gamepad` | | |
| CDC ACM | | ✅ `usb_serial` | ✅ `usb_serial` | | |
| USB MIDI | | ✅ `usb_midi` | ✅ `usb_midi` | | |
| USB MSC | ✅ `fat_ramdisk` | ✅ `usb_msc` | ✅ `usb_msc` | | |
| USBVendor / WebUSB | ✅ `descriptor` / compile | ✅ `usb_vendor` bulk/control/WebUSB URL | ✅ `usb_vendor` bulk/control/WebUSB URL | | ✅ `examples/USBVendor` |
| USB Audio | ✅ compile smoke | ✅ `usb_audio_speaker` / `usb_audio_microphone` / `usb_audio_headset` | 対象外（P4 の Audio は UAC2/HS、loopback は FS 限定） | | ✅ `examples/AudioSpeaker` / `AudioMicrophone` / `AudioHeadset` / `AudioSpeakerM5`（P4 HS） |
| composite（2 クラス同時） | ✅ `composite_reject`（Audio 排他 / MAX_CLASSES） | 予定 `composite_*`（10 ペア） | 予定（peer 合格分のみ） | | |
| examples compile | ✅ `examples_compile` | | | | |

## EspUsbHost 詳細挙動テスト計画

このライブラリを作る理由の1つは、既存 USB device 実装では report descriptor、
report ID、output / feature report、複合 HID などの細かい挙動を十分に制御できず、
対応する `EspUsbHost` 側も細かい自動テストを作りづらかったことです。

`EspUsbDevice` では、Host 側の callback / parser / control transfer を意図的に刺激できる
device sketch を作り、Host 側の細かい挙動を peer / loopback の両方で検証します。

Host 側で怪しい挙動、仕様漏れ、parser の取りこぼし、callback 不整合が見つかった場合は、
Device 側の workaround で隠さない方針です。Device 側は USB device として妥当な descriptor /
report / control 応答を出し、再現できる peer / loopback sketch、期待値、実ログを残します。
そのうえで `EspUsbHost` 側の修正対象として切り出します。Device 側で回避を入れるのは、
USB 仕様上または Arduino-ESP32 / TinyUSB runtime 上の制約として説明できる場合に限ります。

### 追加実装なしで先に確認する項目

現在の keyboard / mouse / composite 実装だけで確認できる Host 側 API です。

| Host 側機能 | Device 側刺激 | 期待 |
|-------------|---------------|------|
| `onKeyboard()` | `keyboard.write()` / `tapKey()` / `pressUsage()` | `pressed` / `released` / `keycode` / `ascii` / `modifiers` が期待通り |
| keyboard layout | ✅ `hid_keyboard_layout`（peer + loopback） | `EN_US` / `JA_JP` で記号キーが期待通り |
| keyboard lock state | Host `setKeyboardLeds()`、Device `onOutputReport()` | NumLock / CapsLock / ScrollLock の output report を受けられる |
| `onMouse()` | `mouse.move()` / `wheel()` / `press()` / `release()` | `x` / `y` / `wheel` / `buttons` / `previousButtons` / `moved` / `buttonsChanged` が期待通り |
| `onHIDInput()` | keyboard / mouse input report | raw input report の address / interface / subclass / protocol / length / bytes が期待通り |
| `onHIDReportDescriptor()` | keyboard / mouse / composite descriptor | report descriptor の interface、reported length、実 length、先頭/末尾 byte が期待通り |
| report ID 分岐 | keyboard + mouse composite | report ID 1 keyboard、report ID 2 mouse が混線しない |

最初に追加する具体テスト:

1. ✅ `loopback/hid_keyboard` に `onHIDInput()` と `onHIDReportDescriptor()` の assert を追加する。
2. ✅ `loopback/hid_mouse` に raw input report byte と parsed mouse event の対応 assert を追加する。
3. ✅ `loopback/hid_keyboard_mouse` に report ID 1 / 2 の raw input assert を追加する。
4. ✅ 同じ観点を `peer/*` にも戻し、S3 2台構成と P4 1台構成の差を記録する。

### Device 側に小さな class 追加で確認する項目

| Host 側機能 | 必要な Device 側追加 | 期待 |
|-------------|----------------------|------|
| `sendSetProtocol()` | ✅ `hid_keyboard` の `onProtocol()` | report protocol request を観測できる |
| `sendHIDReport(... OUTPUT)` | ✅ `hid_vendor` | Host からの Output report の payload / length を検証。P4 loopback では Device 側 report ID は `0` として観測 |
| `sendHIDReport(... FEATURE)` | ✅ `hid_vendor` | Host からの Feature report を検証 |
| `onVendorInput()` | ✅ `hid_vendor` | report ID 6 の vendor input が callback へ届く |
| HID parser fields | ✅ `custom_hid` の descriptor / input から開始 | usage page / usage / bit offset / bit size / logical min/max を検証 |

### HID class 追加で確認する項目

| Host 側機能 | 必要な Device 側 class | 期待 |
|-------------|------------------------|------|
| `onConsumerControl()` | ✅ `hid_consumer_control` | play/pause、mute、volume、next/previous が press/release で届く |
| `onSystemControl()` | ✅ `hid_system_control` | power / sleep が press/release で届く |
| `onGamepad()` | ✅ `hid_gamepad` | fields / fieldCount / changed / rawData / reportData が期待通り |

### HID 以外の Host 詳細テスト

CDC ACM、MIDI、MSC、USBVendor、Audio は Device 側 class 実装が必要です。
`peer/usb_serial`、`peer/usb_midi`、`peer/usb_msc`、`peer/usb_vendor`、`peer/usb_audio_speaker` は
EspUsbDevice 実装へ移行済みです。Audio は speaker sink の peer test（S3, UAC1/FS）まで自動化済み。
P4 の loopback audio テストは意図的に用意しない：P4 では本ライブラリの Audio は UAC2/HS 専用
（`TUD_OPT_HIGH_SPEED`）で、1台 loopback は FS 限定（UTMI PHY をデバイスが握る）のため原理的に
噛み合わない。よって P4 Audio(UAC2/HS) は HS 専用とし実機 HS 手動確認でカバー。将来 P4 2台 HS peer で
UAC2 自動化は可能（`docs/DESIGN_NOTES.ja.md` 参照）。`peer/usb_audio_microphone` が USB Audio source
（マイク）方向をカバー：device が生成 PCM をストリームし、Host が device → Host 受信を検証（S3, UAC1）。
`peer/usb_audio_headset` が両方向同時（1台で speaker + microphone）をカバー。
長時間再生、実音確認、実マイク入力の取り込みは残作業。

endpoint サイズは現状すべての class で FS 固定（HID interrupt=8、CDC/MIDI/MSC/Vendor bulk=64）。
S3(FS) と P4 1台 loopback(FS) では正しいが、P4 を実 HS ホストに繋ぐと bulk が非準拠（HS bulk は 512 必須）。
FS loopback を主対象とする現段階では許容し、正しい解決（per-speed descriptor）は実 PC/HS マイルストーンに
先送りする。詳細は `docs/DESIGN_NOTES.ja.md`「bulk エンドポイントサイズと HS 準拠」。

`peer/usb_serial` は `EspUsbDeviceCdcSerial` の最初の CDC ACM テストです。Host 側は
released `EspUsbHost` の `EspUsbHostCdcSerial` を使い、Device -> Host、Host -> Device、
line coding callback を検証します。default profile は released Host を使い、
`s3_peer_local` は Host 側未リリース修正の任意確認にだけ使います。

`loopback/usb_serial` は同じ観点を P4 1台構成で確認します。CDC endpoint MPS は
FS Host で確保できるよう notification 8 bytes、bulk data 64 bytes とします。

`peer/usb_midi` は `EspUsbDeviceMidi` の最初の USB MIDI テストです。Device -> Host /
Host -> Device の channel voice message と、Host -> Device の短い SysEx packet 分割を
検証します。default profile は released Host を使い、`s3_peer_local` は Host 側未リリース
修正の任意確認にだけ使います。

`loopback/usb_midi` は同じ観点を P4 1台構成で確認します。SysEx は複数 packet が同じ
poll で読めるため、packet 順の逐次待ちではなく、必要な packet を両方観測したことを
assert します。

`peer/usb_msc` は `EspUsbDeviceMsc` の最初の USB Mass Storage テストです。単一 LUN の
RAM disk として、capacity / inquiry / max LUN / sense / test unit ready / synchronize cache /
read / write / multi-block / chunked transfer / out-of-range / failed write を検証します。

`loopback/usb_msc` は同じ観点を P4 1台構成で確認します。RAM disk の全体容量は
16 blocks x 512 bytes とし、chunked transfer では Host 側の 4096 byte chunk 境界も確認します。

`EspUsbDeviceVendor` は HID ではない vendor-specific interface として、descriptor unit test、
`examples/USBVendor` の build-only 確認、`peer/usb_vendor` の interface / bulk endpoint 列挙、
bulk OUT -> Device -> bulk IN echo、application vendor control IN/OUT、WebUSB landing URL 読み出しを
確認します。default profile は released Host を使い、`s3_peer_local` は Host 側未リリース修正の
任意確認にだけ使います。Microsoft OS 2.0 descriptor は Host OS / browser / driver の影響が
大きいため、まず `tests/manual` の手順で確認します。

MSC の transport テストとファイル受け渡しテストは分けます。`peer/usb_msc` /
`loopback/usb_msc` は raw block I/O、SCSI、error path を確認するテストとして維持します。
FAT や SD の使いやすさは別テストにします。

`EspUsbDeviceMscFatRamDisk` を追加したら、最初は Host が mount できる小さい FAT image と、
eject / stop 後に ESP32 側で 8.3 filename の file を scan / read できることを確認します。
firmware update や Wi-Fi 転送は、まず「Host が書いた file を eject 後に Device 側が
byte 列として取り出せる」ことを合格条件にします。

`unit/fat_ramdisk` は、Host mount 前に FAT12 image の基本構造、root entry、cluster chain、
`exists()` / `fileSize()` / `readFile()`、MSC attach と eject callback を検証します。
PC mount / file copy / OS eject は別途 manual または peer テストで確認します。

`EspUsbDeviceMscSdCard` は SD card を block device として公開する手動または任意自動テストから
始めます。Host が MSC として SD を所有している間、ESP32 側は同じ filesystem を mount /
書き込みしないことを仕様条件にします。flash / SPIFFS / LittleFS の直接 MSC 公開は標準
テスト対象にしません。
`examples/MSCSdCard` は build を通すことを最低条件とし、Host OS mount / file write /
eject は `tests/manual` の手順で確認します。

`examples_compile` は `examples/*/*.ino` を列挙し、各 sketch を `arduino-cli compile
--profile s3` で build-only 確認します。examples はユーザー向け API の入口なので、
peer / loopback の実機テストとは別に、全 example が常にコンパイルできることを合格条件にします。

### 複合デバイス（composite）テスト

2 クラスを同時に `addClass` した複合デバイスが、実際に列挙され両クラスが機能するかを
確認します。目的は「どの組み合わせが同時に動くか」を機械判定で確定し、動かない組み合わせは
その原因（USB 仕様・Arduino-ESP32・TinyUSB runtime の制約）を記録して切り分けることです。

#### 現状の構造的制約（テスト設計の前提）

複合は各クラスが core の `tinyusb_enable_interface()` に個別登録する方式です
（[EspUsbDevice.cpp](../src/EspUsbDevice.cpp) の `begin()`）。ここに 2 つの制約があります。

1. **Audio は排他**。Audio class がある場合、`classCount_ != 1` または他クラス併用は
   `begin()` が `ESP_ERR_NOT_SUPPORTED` で失敗する（コードで明示的に禁止）。
   よって Audio × 任意クラスは全て NG が仕様であり、その NG を回帰テストで固定する。
2. **endpoint 番号の採番が 3 方式に分裂**していて互いを認識しない。これが複合破綻の
   最有力原因（仮説）。

   | クラス | interface | EP 採番 | 使う EP 番号 |
   |--------|-----------|---------|--------------|
   | HID（統合 1 本） | 1 | ライブラリ独自カウンタ（1 から） | EP1(OUT), EP2(IN) |
   | CDC | 2 | ベタ書き固定 | EP3(OUT), EP4(IN), EP5(notif) |
   | MIDI | 2 | core `tinyusb_get_free_*` | 動的 IN+OUT |
   | MSC | 1 | core `tinyusb_get_free_duplex` | 動的 duplex×1 |
   | Vendor | 1 | core `tinyusb_get_free_duplex` | 動的 duplex×1 |

   HID の独自カウンタ／CDC の固定値は core アロケータから見えないため、
   `tinyusb_get_free_*` が同じ EP 番号を再配布すると衝突しうる。加えて S3(FS) の物理 EP は
   6 本程度、`MAX_CLASSES = 4`。

#### 対象マトリクス（Audio 除く 5 グループ = C(5,2) = 10 ペア + Audio 排他）

破綻予測は上の EP 採番分析による仮説。テストで確定する。

HID の複合衝突（endpoint がビットマスク未登録 + interface number 焼き込み）は **修正済み・実機確認済み**：
HID を EP1 duplex + `reserve_endpoints=true` にし、HID interface number を core 採番値へ書き換えた
（`docs/DESIGN_NOTES.ja.md`「複合時の HID 採番衝突」）。修正後は全 class が core の動的採番で一貫するため、
**全ペアを個別に検証する必要はなく、予算天井まで積んだ最大構成テストが部分集合を包含する**
（4 クラスが `dup=0 / claimok=1` で機能すれば、その部分集合の各ペアも成立）。

| # | 組み合わせ | 結果 | 根拠 |
|---|------------|------|--------------|
| 1 | HID + CDC | ✅ 実機 OK（`composite_hid_cdc` 4/4） | HID=IF0/EP1、CDC=IF1,2/EP3,4,5 |
| 3 | HID + MSC | ✅ 実機 OK（`composite_hid_msc` 3/3） | 修正後 MSC=IF0/EP2、HID=IF1/EP1、`dup=0 claimok=1` |
| 2,4-10 | 上記以外の非 Audio ペア | ○（最大構成で包含） | 単一 core アロケータで一貫採番。下記 quad/triple がカバー |
| 11 | Audio + 任意 | ✗ NG（仕様） | コードで排他。`unit/composite_reject` で固定 |
| — | HID + bulk Vendor | ✗ 別 issue・未対応 | descriptor 二重記述疑い。`docs/DESIGN_NOTES.ja.md` 参照、スコープ外 |

**S3 の endpoint 予算**: `CFG_TUD_NUM_EPS=6` / `CFG_TUD_NUM_IN_EPS=5`（FIFO 制約で使える IN は実質 4、CDC 併用時 5）。
IN 消費は HID=1 / CDC=2 / MIDI=1 / MSC=1 / Vendor=1。同時搭載の上限は約 4 クラス（`MAX_CLASSES=4` とも一致）。

最大構成テスト（全ペアの代替）:

- `peer/composite_quad_midi`: HID + CDC + MSC + MIDI（IN=5、CDC 込みで天井）。dup=0 / 全 claim / 4 class 機能で
  部分集合（HID+MIDI、MIDI+MSC、CDC+MSC 等）を包含。
- 非 HID の Vendor カバレッジは HID を含まない triple（例 `peer/composite_cdc_msc_vendor`）で確認する
  （HID blob を読まないので bulk Vendor の二重記述 issue を回避できる）。

HID + HID（keyboard + mouse、vendor など）は report ID 多重で単一 HID interface に
なるため、既に `hid_keyboard_mouse` / `hid_vendor` でカバー済み。ここでは扱わない。

#### レイヤ分担（重要な制約）

CDC/MIDI/MSC/Vendor を含む合成 descriptor と EP 採番は `startTinyUsb=false` では走らず、
core の `tinyusb_init()` 実行時にしか確定しない。よって byte 単位の unit 検査は HID 統合分に
限られ、それ以外の合成は実機で列挙して確認する。

- **unit（S3 単体・host 不要）**
  - `unit/composite_reject`: Audio + 各クラス、および `MAX_CLASSES` 超過（5 クラス）で
    `begin()==false` かつ `lastError()==ESP_ERR_NOT_SUPPORTED` を確認（#11）。
    判定ロジックは `startTinyUsb=false` でも通るため host 不要。
- **peer（S3 2 台・host=EspUsbHost / device=EspUsbDevice）** ← 本命
  - 各ペア `peer/composite_<a>_<b>/` を作成。2 段階で判定:
    1. **列挙成功 + EP 重複なし**: host 側で config descriptor をダンプし、全 endpoint
       address に重複が無いこと（`NO_DUP_EP 1`）と interface 番号が 0..N 連番であることを assert。
    2. **両クラスが機能**: 各クラスの最小往復を 1 本ずつ（HID→report、CDC→双方向 1 往復、
       MSC→容量読取、MIDI→packet 往復、Vendor→bulk echo）。既存 peer sketch を部品として合成。
  - device 側 `.ino` は共通出力 `DEVICE_BEGIN ok|ng <errname>` を持つ。
- **loopback（P4 1 台）**: peer で通ったペアのみ回帰。EP 衝突は速度非依存なので peer で足りる
  場合が多い。

#### 共通ユーティリティ（1 度作れば全ペアで再利用）

- host 側: config descriptor をダンプし `NO_DUP_EP 1/0` を出力する判定関数。
- device 側: `DEVICE_BEGIN ok|ng <errname>` の共通出力。
- py 側: 列挙 + EP 重複なしを共通 assert するフィクスチャ。

#### 実行順（段階的）

1. `unit/composite_reject`（Audio 排他 / MAX_CLASSES を回帰固定。host 不要ですぐ走る）。
2. `peer/composite_hid_cdc`（#1、動く見込みの複合で雛形と共通 util を確立）。
3. `peer/composite_hid_msc` / `hid_midi` / `hid_vendor`（#2-4、破綻本命）。列挙失敗や
   EP 重複が出たら `docs/DESIGN_NOTES.ja.md` に「複合時の EP 採番衝突」として記録し、
   根本対策（HID を core アロケータに寄せる／CDC 固定 EP の動的化）を別 TODO 化。
4. 残り CDC 系 / アロケータ系（#5-10）を順次。
5. 結果を本ファイルと `docs/DEVELOPMENT_PLAN.ja.md`（composite の可否と制約）へ反映。

## 初期移行順

1. `unit/compile_smoke`
2. `unit/descriptor`
3. ✅ `peer/hid_keyboard`
4. ✅ `peer/hid_mouse`
5. ✅ `peer/hid_keyboard_mouse`
6. `probe/p4_device_fs_probe`
7. `probe/p4_device_hs_probe`
8. ✅ `loopback/hid_keyboard`
9. ✅ `loopback/hid_mouse`
10. ✅ `loopback/hid_keyboard_mouse`
11. ✅ `peer/hid_keyboard_layout`
12. ✅ `peer/custom_hid`
13. ✅ `peer/hid_vendor`
14. ✅ `peer/hid_consumer_control`
15. ✅ `peer/hid_system_control`
16. ✅ `peer/hid_gamepad`
17. ✅ `loopback/hid_gamepad`
18. ✅ `peer/usb_serial`
19. ✅ `loopback/usb_serial`
20. ✅ `peer/usb_midi`
21. ✅ `loopback/usb_midi`
22. ✅ `peer/usb_msc`
23. ✅ `loopback/usb_msc`
24. ✅ `peer/usb_vendor` bulk/control/WebUSB URL
25. ✅ `loopback/usb_vendor`
26. ✅ `loopback/custom_hid`
27. ✅ `loopback/hid_vendor`
28. ✅ `loopback/hid_consumer_control`
29. ✅ `loopback/hid_system_control`
30. ✅ `peer/usb_audio_speaker`
31. ✅ `loopback/hid_keyboard_layout`
32. （`loopback/usb_audio` は無し：P4 の Audio は UAC2/HS、loopback は FS 限定のため）
33. ✅ `peer/usb_audio_microphone`
34. ✅ `peer/usb_audio_headset`
35. `unit/composite_reject`（Audio 排他 / MAX_CLASSES）
36. ✅ `peer/composite_hid_cdc`（複合の雛形 + 共通 util、4/4）
37. ✅ `peer/composite_hid_msc`（HID 採番衝突の発見 → 修正 → 3/3。`docs/DESIGN_NOTES.ja.md`）
38. `peer/composite_quad_midi`（HID+CDC+MSC+MIDI、予算天井）
39. `peer/composite_cdc_msc_vendor`（非 HID triple、Vendor カバレッジ）

## 合格条件

- descriptor テストはログ確認ではなく byte 列を assert する。
- `unit/compile_smoke` は build-only で Arduino CLI、sketch.yaml、ESP32 board package、ライブラリ解決を確認する。
- peer テストは serial command で device board の挙動を制御する。
- device sketch は Arduino-ESP32 標準の `USB.begin()` を呼ばない。
- P4 テストは selected port、requested speed、TinyUSB rhport、取得できる場合は
  connected speed、VID/PID、interface count、endpoint MPS を出力する。
- 未対応の P4 port / speed 組み合わせは、無言 skip ではなく `xfail` または probe
  結果として明示する。
