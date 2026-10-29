# P4 / P4Xと追加試験環境

調査基準: Arduino-ESP32 3.3.12（IDF5.5.5）、公開EspUsbHost2.10.1、Device製品80e8676。
試験の入口は標準pluginのcombo。独自conftestや旧版testsへの依存は追加しない。

## 接続中の実機

| serial alias末尾 | esptoolの実測 | 現在確認できた結線 |
| --- | --- | --- |
| e0aa24 | ESP32-P4 revision v1.3 | 同一P4のHS–FS loopback。両Device controllerをFS linkで確認 |
| d0b261 | ESP32-P4 revision v1.3 | 書き込み・識別portを確認。native HSポートの接続先/board型番は未確定 |

識別はchip-id（実際はMAC読出し）で行い、2台目へ試験firmwareはまだ書き込んでいない。
P4X実機の存在は確認できていない。board名だけで判断せず、silicon revisionとFQBNを記録する。

## P4Xは名前だけの変更ではない

公式のP4 v3.x guideは対象をESP32-P4NRW16X/NRW32Xとし、旧v1.xからの設計変更を記載する。
CPU、memory/DMA、USB HS設定・PHY制御にも変更があり、既存P4の成功をそのままP4Xの認定にしない。
v3.xではIDF5.5.3以降または6.0以降で対象設定を合わせてrebuildする必要がある。
[公式v3.x User Guide](https://documentation.espressif.com/esp32-p4-chip-revision-v3.x_user_guide_en.html)

PCNはv1.3→v3.2を推奨し、型番末尾X、pin54のNC→VDD_HP変更、16 MB品のPSRAM調達仕様差を記載する。
chipだけ載せ替える計画にはせず、対応boardと回路図で確認する。
[公式PCN202600801](https://documentation.espressif.com/en/PCN202600801_ESP32-P4_Chip_Revision_v3.2_Upgrade_Chip_Revision_v1.3_Demand_Collection_and_EOL_Plan_Description.html)

## 手元のCore/SDKで確認した差

以下はWeb上の推測ではなく、インストール済みCore3.3.12のboards.txt、両SDKのsdkconfig、
hw_ver1/3のsoc/usb_dwc_cfg.hを比較した値。clockはbuild設定であり実測値ではない。

| 項目 | 旧P4用 | P4X用 |
| --- | --- | --- |
| Arduino ChipVariant | prev3 | postv3 |
| library package | esp32p4_es-libs | esp32p4-libs |
| CPU build clock | 360 MHz | 400 MHz |
| SELECTS_REV_LESS_V3 | y | n |
| SDK対応revision範囲（encoded） | 1〜199（v1.x用） | **301〜399（v3.1以上）** |
| HS non-periodic TX queue depth | 4 | 8 |
| HS periodic TX queue depth | 4 | 16 |
| HS NUM_EPS / NUM_IN_EPS / DFIFO_DEPTH | 15 / 8 / 1024 words | 同じ |

menu表示は「v3.00 or newer」だが、今回配布SDKの最小revisionは3.1（encoded301）。v3.0実機も対応済みと扱わない。
この比較ではendpoint/FIFO容量を変更する根拠はない。queue depth差や追加PHY制御は別途実測する。
P4Xで同じchip target名を使えても、旧binaryをそのまま使えることにはならない。

新しいMSC/MIDI probeは`ChipVariant=prev3`の実機4件成功に加え、`postv3`のcompileも成功した。
RISC-V ELF、異なるSDK package、29 source依存、native実装のリンク先を監査した。
**P4Xはcompile-only**で、v1.3実機へpostv3 imageを書き込んでいない。

```sh
cd validation/p4/classes
arduino-cli compile --profile p4x_compile --build-path build/p4x_compile
```

p4x_compileはcomboへ登録しない。通常allは既存v1.3を対象としたS3 47＋P4 10＝57項目を収集する。

## 既存環境で追加実行したこと

[最初のP4記録](../validation/p4/README.ja.md)の6件に、新規MSC＋MIDIの4件31.85秒成功を追加。
4 serial全正常停止、panicなし。4件はcontroller 2種×sector/staging 2構成、各2回開始・終了した。

- RAM MSC: sector511/512、3領域×7 blockの書込み・読出し・Device保存先を照合。24 write＋24 read、各方向85932 byte、容量外guard保持。
- HS-capable controllerのstaging64拒否を2回確認。512へ直してbegin/列挙/通信が回復した。linkがFSでも広告可能なMPS512の予約が必要。
- MIDI: 2 cableの32 event echoを8開始で確認（256往復）。MSCと同じ構成に置く。traffic同時性能の測定ではない。

記録はvalidation/.pytest-resultsのp4-classes-{initial,after}*とp4x-compile-audit.json。
初回はHS controllerへstaging64を指定した試験側の構成誤り。製品を変えず拒否/再設定の検査に直した。
S3 47件、P4初回6件、今回4件は別実行。57件一括成功という記録ではない。

## 追加で必要な環境と進め方

| 確認したいもの | 今ある環境で可能なこと | 追加条件 |
| --- | --- | --- |
| MSC/MIDI、Vendor/CDC/HID | 両controllerのFS linkまで実測済み | なし |
| Audio ISO、Video、DFU、CCID、NCM | 現loopbackでRAM payload/模擬backendの試験を追加可能 | codec/SD/card readerはUSB層の試験には不要。順次probeを追加 |
| PSRAM経由のcopy/ring | Core profileをPSRAM enabledにして実容量を確認するところから可能 | 現profileはdisabled。未初期化をPSRAM非搭載と誤認しない。まず既存boardで試す |
| 480 MbpsのHS Device | 既存P4 2台のHS–HS、またはPCのUSB2 HS Hostへ接続 | native data cableの結線変更。独立log port。descriptor/bus speedがHIGH/MPS512になったことを確認 |
| P4X固有の動作 | postv3 compileは完了 | v3.1以降（新規導入ならv3.2確認品）の実機1台、適合するboard/SDK profile |
| PC OSのclass binding | PCをHS Hostにすれば同時に確認可能 | USB data deviceを実際にそのOSへ接続。serial portのforwardだけでは代替不可 |

今後の優先順はPSRAMの実容量/CPU copy経路、Audio ISOと未確認class、HS実結線、P4X実機。
NCMを同じMCUのlwIP IP間通信だけで合格にしない。raw frame/USB転送量でwire経由を確認する。
DFUはまずRAM backendを使い、実OTA/SDへの書込みは別の試験範囲とする。

HSの最初の試験はhubなし直結。HS Hostは1台loopbackの反対側FS controllerでは代替できない。
board型番とnative connectorの配線を確認し、Host側だけがVBUSを供給する組合せを選ぶ。
公式P4X EV boardにもHS Type-C/Type-A、FS、Serial/JTAGの区別があるため、connectorの形だけで役割を推定しない。
[公式P4X board guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4x-function-ev-board/user_guide.html)

常駐台数を増やす前に、既存2台でHSを確認する。P4Xを追加する場合は5台benchのP4役をP4Xへ置換し、
旧P4は互換性試験時だけ接続する案でよい。P4Xと旧P4の両方を常時必要とはしない。
