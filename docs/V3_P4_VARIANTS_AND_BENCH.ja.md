# P4 / P4Xと追加試験環境

調査基準: Arduino-ESP32 3.3.12（IDF5.5.5）、公開EspUsbHost2.10.1、Device製品80e8676。
試験の入口は標準pluginのcombo。独自conftestや旧版testsへの依存は追加しない。

## 接続中の実機

設備の正本はOEP benchの台帳`/home/mt/dev_oep/oep-bench/hosts/native-linux/README.ja.md`と
同ディレクトリの`inventory.json`。位置`N-M`はハブNのport M。下表は2026-10-30の照合結果。

| 位置 | serial alias末尾 | esptoolの実測 | 結線と用途 |
| --- | --- | --- | --- |
| 2-1 | e0aa24 | ESP32-P4 v1.3 | 同一P4のHS–FS loopback。combo `p4`/`p4_psram` |
| 2-3 | d0b261 | ESP32-P4 v1.3 | CH340 UARTで制御。2-4とHS直結。combo `p4_p4x_hs`/`p4x_p4_hs` |
| 2-4 | d71d86 | ESP32-P4 v3.2（P4X） | USB JTAG/serial（303a:1001）で制御。2-3とHS直結 |
| 3-3/3-4 | 19c5 | ESP32-P4 v3.2（P4X） | 1枚のboard。3-3がUSB JTAG/serialの制御/log、3-4がnative HSでLinux PCへ直結。combo `p4x_pc` |

P4X 2台はrevision v3.2で、postv3 SDKの対応範囲（min3.1）を満たす。chip-idは機器lockを取って実測した。
3-4はOSから480M（libusbのspeed=3）で見える。台帳上は汎用機材で、試験firmwareの書込みと書き戻し不要は
台帳の規則に従う。3-3と3-4は同一boardなので、手動操作では両方の機器lockを取る。
3-4の既存firmwareは台帳上OEP probeとして管理しないDFU対応firmwareで、上書きしてよい。

PC Host用に`validation`の依存へpyusbを追加した。sg3_utils/dfu-utilは未導入で、BOT/DFUの検査は
pyusbから生のcontrol/bulk転送で行う。既存のudev規則で一般userが開けるのは`1209:0001`など一部の
VID/PIDだけなので、PC試験のsketchはこのVID/PIDを使うか、規則の追加をユーザーへ依頼する。
OSのclass driver（usb-storage等）が先にbindする構成ではdetachの扱いを試験側で明示する。

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
| Audio ISO | UAC1/UAC2、両controllerで同時duplexとfeedbackを確認済み | codec/I2S不要。FS link・mono16 kHz/16 bitの範囲 |
| Video、DFU、CCID、NCM | **両controllerで基本通信/再開始を確認済み** | SD/card reader不要。Videoはsynthetic payload、DFUはRAM backend、NCMはraw frame |
| PSRAM経由のcopy/ring | **既存boardで32 MiB確認、MSC保存先・Vendor RX/TX ringの実通信成功** | 専用p4_psram profileで有効化。追加RAM board不要。直接PSRAM DMAの検証ではない |
| 480 MbpsのHS Device | 既存P4 2台のHS–HS、またはPCのUSB2 HS Hostへ接続 | native data cableの結線変更。独立log port。descriptor/bus speedがHIGH/MPS512になったことを確認 |
| P4X固有の動作 | postv3 compile完了。P4X–P4 HS直結もユーザーから確認 | 初回にrevision実測、適合SDK/FQBN、HWCDC logとHSデータの独立動作を検査 |
| PC OSのclass binding | PCをHS Hostにすれば同時に確認可能 | USB data deviceを実際にそのOSへ接続。serial portのforwardだけでは代替不可 |

既存FS loopbackで全10 classの基本通信とPSRAM利用を確認した。次の機材依存gateはHS実結線とP4X実機。
NCMを同じMCUのlwIP IP間通信だけで合格にしない。raw frame/USB転送量でwire経由を確認する。
DFUはまずRAM backendを使い、実OTA/SDへの書込みは別の試験範囲とする。

HSの最初の試験はhubなし直結。HS Hostは1台loopbackの反対側FS controllerでは代替できない。
board型番とnative connectorの配線を確認し、Host側だけがVBUSを供給する組合せを選ぶ。
公式P4X EV boardにもHS Type-C/Type-A、FS、Serial/JTAGの区別があるため、connectorの形だけで役割を推定しない。
[公式P4X board guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4x-function-ev-board/user_guide.html)

常駐台数を増やす前に、既存2台でHSを確認する。P4Xを追加する場合は5台benchのP4役をP4Xへ置換し、
旧P4は互換性試験時だけ接続する案でよい。P4Xと旧P4の両方を常時必要とはしない。

## PSRAMの追加結果

probe53bcfeb、専用PSRAM enabled profileの2件82.35秒成功。両controllerで各2開始、
2 logs正常停止/panicなし。実測revision103（v1.3）、CPU360 MHz、PSRAM/外部heapとも33554432 byte。
MSCの保存先とVendor RX/TX ringをMALLOC_CAP_SPIRAMで確保し、外部RAM pointerであることも検査した。
MSCは各方向84 block/43008 byte、Vendorは160往復/各方向153664 byte。ring容量8192 byteを跨いで
折り返す総量を流し、各領域の容量外guardとheap整合性を確認した。USB DMA stagingは内部RAMのまま。

postv3＋PSRAM enabledのcompile/ELF/SDK package/29依存も成功。P4X実機の代替とはしない。
記録はvalidation/.pytest-results/p4-psram-{initial,native-audit}*、p4x-psram-build.txt。
標準allのこの時点の収集はS3 47＋P4通常10＋PSRAM2＝59件（実行結果は各単位で別記録）。
PSRAM comboは通常P4と同じ物理boardなので、実機pytestを並列起動しない。

## Audio ISOの追加結果

probe e577698。UAC1/UAC2×両controllerの4件33.95秒成功、4 logs正常停止/panicなし。
各caseで2開始し、同時にHost→Deviceへ5a、Device→Hostへ6bのPCMを流した。
16 kHz/mono/16 bit。開始後100 msはring充填のwarmupとし、その後約350 msを照合した。
8測定でplayback89600 byte、capture88064 byte、payload不一致なし。測定中の
playback overrun/capture underrunとも0、active formatのsnapshotも一致した。
UAC1は明示feedbackなし、UAC2はfeedback更新あり/reject0を確認した。

初回はUAC1にも明示feedbackを要求した試験条件で失敗。PCM自体は正しく、
protocolごとの契約へ試験を直して片方向4件成功後、duplex4件も成功した。製品の変更はない。
postv3のAudio compile/ELF/29依存も監査済み。新たな機材・SDK patchは不要だった。
記録p4-audio-{initial,after,duplex}*、p4x-audio-compile-audit.json。

Audio追加時点の収集はS3 47＋P4通常14＋PSRAM2＝63項目だった。

## 現在の到達点（2026-10-30）

CCID4、Video4、DFU2、NCM4の14 caseも追加し、P4は通常28＋PSRAM2の計30 caseを各単位で成功した。
全10 classの基本通信・両controllerのFS link・再開始を確認。最新all収集はS3 47＋P4 30＝77件で、
全77一括実行の結果ではない。[詳細と制約](../validation/p4/README.ja.md)。製品sourceは80e8676のまま。

2026-10-30にユーザーからP4X d71d86–P4 d0b261のHS直結を確認した。以下のpeer計画で進める。
P4Xの動作は現時点でcompileまで、HS実機試験はまだ行っていない。常駐benchのP4枠の置換案は維持する。
未選択classの除去とnative controller出所も追加4 classのELFで確認した。


## HS直結peerのprofileと実行計画（2026-10-30）

普通のprimary Host＋peer_device構造を使い、テスト関数はdutとpeers["device"]を受け取る。
plugin1.10.0/698bb29のREADME/TESTING_ADVANCEDを再確認した。結線で決まる組を標準comboへ列挙し、
profileの直積・独自conftest・テスト本文での設備選択を追加しない。

### 名前とport割当

profileは「SoC世代＋HS＋役割」。MACやtty番号を名前へ埋めず、物理boardは.envへ割り当てる。
旧単体loopbackのp4_loopback/p4_psramとは別の名前にする。

| profile | 役割 | .envのkey | 制御/log alias末尾 |
| --- | --- | --- | --- |
| p4_hs_host | primary Host | TEST_SERIAL_PORT_P4_HS_HOST | esp32-p4-80f1b2d0b261 |
| p4x_hs_device | peer Device | TEST_SERIAL_PORT_PEER_DEVICE_P4X_HS_DEVICE | esp32-series-80f1b2d71d86 |
| p4x_hs_host | primary Host | TEST_SERIAL_PORT_P4X_HS_HOST | esp32-series-80f1b2d71d86 |
| p4_hs_device | peer Device | TEST_SERIAL_PORT_PEER_DEVICE_P4_HS_DEVICE | esp32-p4-80f1b2d0b261 |

.env.exampleへkeyを追加し、ignoredな.envには指定された実portを設定した。
2026-10-30にcomboもpyprojectへ登録した。HS sketch/profileは未追加で、収集件数は変わらない。

追加するcomboはHostを先に並べた名前とする。

```toml
arduino_combos = [
  # 既存s3/p4/p4_psramは残す。
  "p4_p4x_hs: primary=p4_hs_host,device=p4x_hs_device",
  "p4x_p4_hs: primary=p4x_hs_host,device=p4_hs_device",
]
```

最初は安定済み旧P4 Host→新規P4X DeviceでDevice実装を確認し、その後P4X Host→旧P4 Device。
逆向きは同じ結線でVBUS供給元/connector役割を切替できることを確認してから有効化する。
ソフトのroleだけを替えて電源側も切り替わったと仮定しない。必要なら先に一方向だけを登録する。
同じ2台を再利用するため逐次実行とし、終了・資源解放を確認してから次comboへ移る。

### build設定と通信経路

| 対象 | FQBN案 | Serialの経路 |
| --- | --- | --- |
| 旧P4両role | esp32:esp32:esp32p4:ChipVariant=prev3,USBMode=default,CDCOnBoot=default | UART0→CH340 |
| P4X両role | esp32:esp32:esp32p4:ChipVariant=postv3,USBMode=hwcdc,CDCOnBoot=cdc | HWCDC/JTAG |

Core3.3.12のboards.txt/HardwareSerial.hでSerialの割当を確認した。
P4Xを従来のUSBMode=default/CDCOnBoot=defaultでbuildすると、SerialはUART0となって今回のCDCにlogが出ない。
USBMode=hwcdcは制御/logの選択であり、試験データにTinyUSB CDCを使う設定ではない。
試験対象はEspUsbDeviceのHS controllerと、相手側EspUsbHostのHIGH_SPEED port。
P4Xでは独立したHWCDC/JTAGを保持し、試験対象のCDC classへ制御/logを混ぜない。
初回はPSRAM無効の小さな構成から始め、後段でenabledの別profileを加える。
P4Xのchip revisionを読んでpostv3 SDKの対応範囲（今回min3.1）を満たすことを確かめてからflashする。
Hostも含めてCore/公開EspUsbHost2.10.1を固定し、build_opt.hによる試験条件の切替はしない。

### 共通peer試験へ段階的に広げる

新しい共通入口をvalidation/peer/へ置き、最初のvendor_linkで両HS comboを扱う。
同じsketchのs3_host/s3_device profileと既存s3 comboも使えるようにし、共通の契約をFS/HSで比較する。
その段階でpyprojectのtestpathsへpeerを追加する。既存s3/や単体p4/を一度に移動しない。

既存のS3試験をそのまま全てHSへ流すのは避ける。MPS64の固定期待、FSのFIFO/ISO interval、
S3専用register、endpoint数上限を前提にしたcaseがある。共通化するcaseだけ速度/能力から期待を決め、
FS固有のcaseは現在の場所へ残す。性能や機能を同じに見せるためのsilent fallbackはしない。

| 段階 | 確認すること |
| --- | --- |
| 0 実機識別と経路 | 両chip revision、対応FQBN/SDK、別log経路、停止した状態から開始、HostとDeviceが別boardであること |
| 1 HS列挙＋Vendor copy | HostでUSB_SPEED_HIGH、実列挙descriptorのbulk MPS512をassert。VID/PID/product照合、双方向payload、short/ZLPと511/512/513、4095/4096/4097等の境界 |
| 2 借用と再開始 | TX/RXの返却status/id/実長、cancel/reset/endで一度返却、guard保持。65535/65536/65537も対象。RX容量は実MPSへ切上げ、3回程度のbegin/endと役割交代後の再実行 |
| 3 よく使うclass | CDC期限/flush、HID、RAM MSC、MIDI、CCID、RAM backend DFU。まず単独、次に代表的な複合構成 |
| 4 HSの周期転送 | UAC1/UAC2 duplex/feedback、Video ISO/Bulk。HS descriptor/interval/FIFOを個別に検査し、FS時の値をそのまま期待しない |
| 5 NCMと外部RAM | raw Ethernet/NTBのwire往復。P4X PSRAM保存先/ringも追加。MCU内IP routingや直接PSRAM DMAを検証の代わりにしない |

copy/borrowedの大きな転送は別caseにし、最大長のbufferを全classへ常駐させない。
Vendor INの主経路は公開Hostのcache準備済みqueue＋callbackで内容を照合する。
既知のvendorReadSync cache問題は別のHost問題として記録し、HS成立の必須試験へ混ぜない。
FSへfallbackした場合はHS試験失敗とする。build/接続/timeoutの問題をskipで隠さない。
今回の目的は動作と契約の確認で、旧版とのbenchmarkや最大帯域の最適化を先にしない。

実装後の入口は次の形にする。現在はまだHS profile/試験未追加なので、このコマンドの実行段階ではない。

```sh
uv run --locked --env-file .env pytest peer/vendor_link --combo p4_p4x_hs -q -rs
uv run --locked --env-file .env pytest peer --combo p4_p4x_hs --combo p4x_p4_hs -q -rs
uv run --locked --env-file .env pytest --combo all -q -rs
```

--combo allは各sketchに存在するprofileだけを適用するので、S3/単体loopback/HS peerを一発で逐次実行できる。
このため既存S3 sketchへ未検証のP4 profileを機械的に追加する必要はない。
HS用profile/試験を追加した時点でcollect-onlyのnodeidと件数、port割当を確認する。
現時点では.env割当とcombo登録だけで、現行allの収集80件を増やしていない。

## HS直結の列挙とVBUS（2026-10-31）

probe `validation/p4_hs/link`。両comboとも1件ずつ成功した（旧P4 Host 24.15秒、P4X Host 83.57秒。後者は初回build込み）。
各向きでvbusSensingを無効/有効にして2回ずつ開始し、Hostは1209:0002をspeed=2（HS）で読んだ。
Device側のGOTGCTL BSesVld（bit19）は、Hostを開始する前から次の値だった。

| Device | vbusSensing有効時のBSesVld | 列挙 |
| --- | --- | --- |
| P4X（2-4） | 1（Host開始前から） | HS |
| 旧P4（2-3） | 0（Host開始後も） | HS（BSesVld=0のままでも成立） |

旧P4ではBSesVldがVBUSの有無を示さない。ユーザーによると2-3と2-4はどちらもhubのportから給電されており、
VBUSの向きで試験は変わらないので調べない（台帳oep-bench d7ce197）。片方の電源断はbenchの規則どおり両portをまとめて切る。
P4X側で見えたVBUSも、Host側のsoftwareが出したものではない。BSesVldはHost開始前から1だった。
逆向きcomboもこの結線で列挙できたので、試験を有効化した。
