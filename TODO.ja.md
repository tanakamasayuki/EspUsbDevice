# TODO

v3 の残作業の一覧です。各項目に「何をすれば完了か」を書きます。
試験の仕組みは [tests/README.ja.md](tests/README.ja.md)、リリース手順は [docs/development/release.ja.md](docs/development/release.ja.md) にあります。

- ここに書いたことを、実装や検証が済んだ扱いにしません。
- 別の機種や別の Host での成功を、その項目の代わりにしません。

## 機種と Core

- [ ] **ESP32-S2 の ROM DFU への移行**
  - S2 の USB の動作は、S3 との FS 直結の両方向（`s3_s2`、`s2_s3`）と Linux PC（`s2_pc`）で確かめています。
  - 完了の条件：S2 の native USB を PC につないだ状態で `rebootToRomDfu()` を呼び、ROM の DFU（303a:0002）として列挙し、`dfu-util` で書き込める。
- [ ] **ESP32-S31 への対応（新しい組 P4X–S31）**
  - いまは未対応です。build には Arduino-ESP32 4.0.0-RC1 以降が要ります。
  - S31 の USB（データシート v0.5 §4.2.2.6）：OTG は HS の 1 本だけ（HS／FS で Host と Device。endpoint 1〜15、IN は EP0 を含め 8 本、FIFO 4 KB）。FS の OTG はなく、USB-Serial/JTAG（FS、GPIO33/34）が別にある。
  - 結線：S31-Function-CoreBoard-1 の Type-A（HS OTG）と、新しい P4X の HS の口を、普通の A–C ケーブルで直結する。Type-A の VBUS は TPS2051C の EN が 5 V 固定で常時出るので、P4X の HS の口が VBUS を出さないこと（2-4 と同じ WT9932P4X-TINY なら出さない見込み）を到着時に確かめる。S31 の VBUS が P4X へ回り込むので、電源は両方の port をまとめて切る。制御は両方とも USB-Serial/JTAG。
  - 完了の条件：
    - 使う Core の版を固定し、S31 の target / FQBN / SDK / toolchain で library が compile できる。
    - S31 の HS port の能力を `src/internal/EspUsbDwc2Platform.cpp` の port 表に、資料と register の値で確かめて登録する。PHY と cache の処理も確かめる。`tests/sketch_support/probe_link.h` の HS の判定に S31 を加える。
    - combo `p4x_s31_hs`（P4X Host → S31 Device）で、`tests/peer/` の共通の probe と HS 専用の probe が通る。
    - EspUsbHost を S31 で Host として動かし、combo `s31_p4x_hs`（S31 Host → P4X Device）でも全件が通る。Device の成功だけで逆向きも対応済みにしない。
- [ ] **HS 直結の組の整理**
  - 2-3（P4 v1.3）⇄ 2-4（P4X）の HS 直結は、当面残す。
  - 完了の条件：`s31_p4x_hs` で P4X の HS の Device 試験が全件、安定して通るようになったら、2-3 ⇄ 2-4 を撤去するか決める。撤去したら、P4 v1.3 の Device 試験は 2-1 の FS loopback だけになることを tests/TEST_PLAN に書く。
- [ ] **Arduino-ESP32 4 系の互換性**
  - 基準は 3.3.12 のままにします。4 系は別の profile と combo にします。
  - 完了の条件：
    - 4 系の版（最初は 4.0.0-RC1）を固定した profile で、全 example と代表の probe が compile できる。
    - S3 と P4 / P4X で、列挙、CDC / Vendor の往復、開始と終了の短い実機試験が通る。
    - Core の差で直した点と、確かめた版を README に書く。RC での結果を 4 系全体の対応にはしない。正式版が出たら同じ手順でやり直す。

## PC OS との互換性

- [ ] **Windows / macOS / Linux での class の動作**
  - いまは Linux PC で MSC の媒体交換と DFU DETACH を pyusb で確かめただけです。Windows と macOS は未確認です。
  - 完了の条件（各 OS で）：
    - 各 class が OS 標準の driver に bind する：CDC ACM、HID（Keyboard / Mouse / Gamepad / Consumer / System）、MIDI、MSC、UAC1 / UAC2、UVC、CDC-NCM、CCID、DFU。
    - CDC の送受信、HID の入力、MSC の mount / 書込み / eject、Audio の再生と録音、UVC の表示、NCM の通信、CCID の APDU、DFU tool での書込みができる。
    - Vendor が WinUSB / libusb / WebUSB で claim でき、MS OS 2.0 descriptor で Windows が driver を自動で当てる。
    - UAC2 の FS feedback（10.14、3 byte）を Windows と macOS の UAC2 driver が受け付ける。
    - 結果を OS の版とともに README の対応表に書く。v2 での PC の結果を v3 の結果として扱わない。

## 実媒体

- [ ] **SD card の MSC helper**
  - 完了の条件：実際の SD card を `EspUsbDeviceMscSdCard` で公開し、PC から mount・読書き・eject ができ、Device 側から内容が一致する。抜去と再挿入の通知も確かめる。
- [ ] **OTA（firmware update）の実機**
  - いまは DFU を RAM backend でしか確かめていません。
  - 完了の条件：DFU、MSC の firmware disk（`EspUsbDeviceMscFirmwareDisk`）、CDC / Vendor / HTTP の各経路で、実際の OTA partition へ書き込み、検証し、再起動後に新しい firmware が動く。途中の中断と検証失敗で、元の firmware が残ることも確かめる。

## Host ライブラリ（EspUsbHost）側の未解決事項

Device 側に回避の動作は加えません。Host 側で直し、Device の試験で確かめます。

- [ ] **MSC の data STALL の後の CSW の回収**
  - EspUsbHost が data STALL の後に CSW を読まずに終わり、後の command が一件ずれる。reset recovery もしない。
  - 完了の条件：Host の現行版で再現し、HALT 解除 → CSW 回収 → 後の command が正しく進むことを、Device の MSC 抜去（02/3A/00）の試験で確かめる。
- [ ] **P4 の同期 Vendor IN（`vendorReadSync()`）の cache 処理**
  - P4 で、受信の最後の byte が化ける。Host の同期経路に DMA 前の cache 処理がない。
  - 完了の条件：EspUsbHost で修正が公開され、P4 の loopback で同期経路の往復が通る。
- [ ] **NCM の `networkClose()` の後の Host 再開始**
  - `networkClose()` → Device の終了 → Host の終了の順にすると、次の Host の開始が失敗する。
  - 完了の条件：Host 側で原因を切り分けて直し、その順序を含む回帰試験を `tests/` に足して通す。

- [ ] **SET_CONFIGURATION で endpoint を開けない Device を列挙すると Host が panic する**
  - P4X の EspUsbHost 2.10.1 で、送信 FIFO に収まらない構成の Device（P4 HS で Vendor 6 個）を列挙すると、ROM 内で Load access fault になった。Device 側はこの構成を `begin()` で拒否するようにしたので、この Device からは再現しない。
  - 完了の条件：構成の失敗を返す Device（試験用に意図的に作る）で Host 側を再現し、Host が panic せずに列挙の失敗として扱うように直す。

## 実装の確認事項

- [ ] **Audio の format の上限が HS でも FS の値**
  - `addFormat()` は、format の packet 長を FS の上限（1023 byte / packet）で検査する。HS でしか表せない大きな format（高 rate・多 channel）は使えない。
  - 完了の条件：HS の上限（1024 byte / microframe）で検査するか、FS の上限を仕様として残すかを決める。HS で検査する場合は、FS へ fallback したときの扱い（その format を広告しない、または `begin()` で拒否）も決め、HS 直結の audio 試験で確かめる。残す場合は Audio の文書に制限として書く。

- [ ] **ESP32-P4 の FS controller の pin の切替え**
  - FS controller の pin（GPIO26/27 と 24/25）の入れ替えは、例（P4FullSpeedDevice）がスケッチから ESP-IDF の HAL（`usb_wrap_ll_phy_select`）を呼ぶ形のままで、ライブラリの API はない。
  - 完了の条件：`EspUsbDeviceConfig` で選べるようにするかを決める。する場合は、PHY の初期化の中で切り替え、P4 の FS で列挙を確かめる。

## リリース

- [ ] **全体の回帰試験と全 example の compile**
  - 完了の条件：リリースする commit で、`tests/` の `--combo all` が全件通り、`tools/build_check.py` が全 profile で通る。

## examples

- [ ] **examples の実機確認**
  - いまは build（`tools/build_check.py`）だけを確かめています。
  - 完了の条件：
    - 各 example を、README に書いた board と結線で実機に書き込み、README の手順どおりに動く。
    - PC を相手にする example は、Windows / macOS / Linux のうち確かめた OS を README に書く。
    - M5 系（`*M5`）、SD card、HTTP など外部の機材が要る example は、その機材で確かめる。
    - 確かめていない example と profile を一覧にして README に残す。
