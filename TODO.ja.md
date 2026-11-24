# TODO

v3 の残作業の一覧です。各項目に「何をすれば完了か」を書きます。
試験の仕組みは [tests/README.ja.md](tests/README.ja.md)、リリース手順は [docs/development/release.ja.md](docs/development/release.ja.md) にあります。

- ここに書いたことを、実装や検証が済んだ扱いにしません。
- 別の機種や別の Host での成功を、その項目の代わりにしません。

## 機種と Core

- [ ] **ESP32-S2 の ROM DFU への移行**
  - S2 の USB の動作は、S3 との FS 直結の両方向（`s3_s2`、`s2_s3`）と Linux PC（`s2_pc`）で確かめています。
  - 完了の条件：S2 の native USB を PC につないだ状態で `rebootToRomDfu()` を呼び、ROM の DFU（303a:0002）として列挙し、`dfu-util` で書き込める。
- [ ] **ESP32-S31 への対応（組 P4X–S31）**
  - S31 の USB（データシート v0.5 §4.2.2.6）：OTG は HS の 1 本だけ（HS／FS で Host と Device。endpoint 1〜15、IN は EP0 を含め 8 本、FIFO 4 KB）。FS の OTG はなく、USB-Serial/JTAG（FS、GPIO33/34）が別にある。
  - Device は HS で動きます。combo `p4x_s31_hs`（7-2 の P4X Host → 7-1 の S31 Device）と `s31_p4x_hs`（S31 Host → P4X Device）で、`tests/peer/` の共通の probe と HS 専用の probe が通ります。S31 が Host 役のときは、Arduino-ESP32 の S31 の libs が Host の control 転送を 256 byte に制限しているので、それを超える probe を外しています（上流に報告済み）。
  - `EspUsbController::FullSpeed` は FS の OTG がないので `ESP_ERR_NOT_SUPPORTED`。`rebootToBootloader()` は system の reset で入り、ROM の loader が UART0 で応えることを実機で確認（USB Serial/JTAG は未結線で未確認）。UF2 の family ID は microsoft/uf2 の登録値 `0x3101f7c1`。
  - 結線：S31-Function-CoreBoard-1 の Type-A（HS OTG）と P4X の HS の口を直結。VBUS は両方から出るが、同じハブの同じ電圧。制御と log は、S31 が UART（CP2102N）、P4X が内蔵の USB-Serial/JTAG。
  - 完了の条件：
    - Arduino-ESP32 の S31 の libs で Host の control 転送の上限が広がったら、`s31_p4x_hs` で外している probe を戻す。
- [ ] **HS 直結の組の整理**
  - 2-3（P4 v1.3）⇄ 2-4（P4X）の HS 直結は、当面残す。
  - 完了の条件：`s31_p4x_hs` で P4X の HS の Device 試験が全件、安定して通るようになったら、2-3 ⇄ 2-4 を撤去するか決める。撤去したら、P4 v1.3 の Device 試験は 2-1 の FS loopback だけになることを tests/TEST_PLAN に書く。
- [ ] **Arduino-ESP32 4 系への移行**
  - `sketch.yaml` は 4 系の版で固定しています。4 系に S2 用の USB Host Library がないので、S2 の Host 役（`s2_host`）だけ 3 系の版のままです。
  - 完了の条件：
    - 公開版の EspUsbHost を Host 役にして、全 combo が通る。

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

## 実装の確認事項

- [ ] **Audio の format の上限が HS でも FS の値**
  - `addFormat()` は、format の packet 長を FS の上限（1023 byte / packet）で検査する。HS でしか表せない大きな format（高 rate・多 channel）は使えない。
  - 完了の条件：HS の上限（1024 byte / microframe）で検査するか、FS の上限を仕様として残すかを決める。HS で検査する場合は、FS へ fallback したときの扱い（その format を広告しない、または `begin()` で拒否）も決め、HS 直結の audio 試験で確かめる。残す場合は Audio の文書に制限として書く。

- [ ] **ESP32-P4 の FS controller の pin の切替え**
  - FS controller の pin（GPIO26/27 と 24/25）の入れ替えは、例（P4FullSpeedDevice）がスケッチから ESP-IDF の HAL（`usb_wrap_ll_phy_select`）を呼ぶ形のままで、ライブラリの API はない。
  - 完了の条件：`EspUsbDeviceConfig` で選べるようにするかを決める。する場合は、PHY の初期化の中で切り替え、P4 の FS で列挙を確かめる。

## 機能の追加

- [ ] **MSC の LUN を複数持つ**
  - 用途：複数スロットのカードリーダー、CD-ROM（ドライバー入り）と書き込める領域の組み合わせなど。USB Host の対向試験（LUN 0/1 を切り替えて読み書きし、LUN ごとに媒体の有無を扱う）の相手にも使う。
  - いまは `maxLun()` が 0 で、媒体の状態・容量・読み書きのコールバックが1組しかない。
  - 完了の条件：入れるかを決める。入れる場合は、LUN ごとの媒体・容量・コールバックと、LUN ごとの抜去・挿入を、Host との往復と PC の mount で確かめる。

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
