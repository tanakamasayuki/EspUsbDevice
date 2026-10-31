# P4 / P4Xと試験環境

ESP32-P4（旧P4、revision v1.x）とP4X（revision v3.x）での、v3の確認範囲と試験環境の記録。
基準はArduino-ESP32 3.3.12（IDF 5.5.5）と公開EspUsbHost 2.10.1。
試験の入口は標準pluginのcomboだけで、独自conftestや旧版testsには依存しない。

## いまの到達点（2026-10-31）

| 範囲 | 状態 | 主な証拠 |
| --- | --- | --- |
| 旧P4のFS loopback（同じboardのHS controller↔FS controller） | 全10 classの基本通信、両controller、再開始を確認 | [P4記録](../validation/p4/README.ja.md)、[FS loopbackでの結果](#fs-loopbackでの結果2026-10-30まで) |
| 480 MbpsのHS直結（旧P4↔P4X、両方向） | 確認済み。既存S3 peer試験39本＋HS専用peer試験が両方向で成功 | [S3 peer試験をHS直結へ広げた結果](#s3-peer試験をhs直結へ広げた結果2026-10-31) |
| P4X Device → Linux PC（HS） | MSC・DFUなどをpyusbのwire検査で確認 | combo `p4x_pc`、[MSC](V3_MSC_API.ja.md)、[DFU](V3_DFU_API.ja.md) |
| PSRAM | bench 4台とも32 MiB。FS loopbackとHS直結の両方で、MSC保存先とVendor ringをPSRAMに置いた通信が成功 | [PSRAM](#psram) |
| HSで見つけた製品の不具合 | 2件修正（OUT DMAのcache書戻し、ISO OUTの偶奇） | [見つけて直した製品の不具合](#見つけて直した製品の不具合) |

未確認のもの：全comboの一括再実行、Windows/macOSのclass binding、USB DMAがPSRAMを直接読み書きする構成（製品は内部RAMのstagingを使い、借用転送ではPSRAMを拒否する）。

## 接続中の実機

設備の正本はOEP benchの台帳`/home/mt/dev_oep/oep-bench/hosts/native-linux/README.ja.md`と
同じディレクトリの`inventory.json`。位置`N-M`はハブNのport M。

| 位置 | serial alias末尾 | chip（esptool実測） | PSRAM | 制御/log | 結線と用途 |
| --- | --- | --- | --- | --- | --- |
| 2-1 | e0aa24 | ESP32-P4 v1.3 | 32 MiB | CH343 UART | 同じboardのHS口とFS口を結線（loopback）。combo `p4`/`p4_psram` |
| 2-3 | d0b261 | ESP32-P4 v1.3 | 32 MiB | CH340 UART | 2-4とHS口どうしを直結。combo `p4_p4x_hs`/`p4x_p4_hs` |
| 2-4 | d71d86 | ESP32-P4 v3.2（P4X） | 32 MiB | USB Serial/JTAG（303a:1001） | 2-3とHS直結 |
| 3-3/3-4 | 19c5 | ESP32-P4 v3.2（P4X） | 32 MiB | 3-3がUSB Serial/JTAG | 1枚のboard。3-4のHS口をLinux PCへ直結。combo `p4x_pc` |

- PSRAMはP4系では必須で、容量はboardで異なる。上の値は`validation/p4_hs/psram`で実測し、台帳にも反映した（oep-bench 3ddf9d5）。chip revisionは旧P4が103、P4Xが302。
- P4X 2台はrevision v3.2で、postv3 SDKの対応範囲（v3.1以上）を満たす。
- 3-4はOSから480 Mbps（libusbのspeed=3）で見える。3-4の既存firmwareは台帳上、上書きしてよいものとして扱われている。
- 2-3のHS口から2-4へ電源が回り込む（[HS直結の列挙とVBUS](#hs直結の列挙とvbus2026-10-31)）。片方の電源を切るときは、benchの規則どおり両portをまとめて切る。
- 機器のlockは試験pluginが取る。手作業でboardを触る場合も、OEPのbench.lockは使わない（OEPの道具専用）。

PC Host用に`validation`の依存へpyusbを入れてある。sg3_utils/dfu-utilは入れていないので、BOT/DFUの検査はpyusbで生のcontrol/bulk転送を組む。
既存のudev規則で一般userが開けるのは`1209:0001`など一部のVID/PIDだけ。PC試験のsketchはこのVID/PIDを使うか、規則の追加をユーザーへ頼む。
OSのclass driver（usb-storage等）が先にbindする構成では、detachの扱いを試験側で明示する。

## comboと実行方法

| combo | primary | peer device | 内容 |
| --- | --- | --- | --- |
| `p4` | p4_loopback（2-1） | なし | 旧P4のloopback |
| `p4_psram` | p4_psram（2-1） | なし | 同じboardをPSRAM有効でbuild |
| `p4_p4x_hs` | p4_hs_host（2-3 旧P4） | p4x_hs_device（2-4 P4X） | HS直結。旧P4がHost |
| `p4x_p4_hs` | p4x_hs_host（2-4 P4X） | p4_hs_device（2-3 旧P4） | HS直結。P4XがHost |
| `p4x_pc` | p4x_hs_pc（3-3/3-4） | なし | P4XをLinux PCへHS直結 |

profile名は「SoC世代＋HS＋役割」。MACやtty番号は名前へ入れず、物理boardは`.env`で割り当てる（keyは`.env.example`を参照）。
同じboardを複数のcomboが使うので、実機試験は逐次実行する。

```sh
cd validation
uv run --locked --env-file .env pytest peer --combo p4_p4x_hs --combo p4x_p4_hs -q -rs
uv run --locked --env-file .env pytest --combo all --maxfail=1000 -q -rs   # 全件。既定は--maxfail=1
```

`--combo all`は各sketchに存在するprofileだけを適用する。peer試験（`peer/`と、HS profileを持つ`s3/`）はHS comboでも収集される。

### build設定

| 対象 | FQBN | Serialの経路 |
| --- | --- | --- |
| 旧P4（CH340/CH343のboard） | `esp32:esp32:esp32p4:ChipVariant=prev3,USBMode=default,CDCOnBoot=default` | UART0→USB-UART |
| P4X（USB Serial/JTAGのboard） | `esp32:esp32:esp32p4:ChipVariant=postv3,USBMode=hwcdc,CDCOnBoot=cdc` | HWCDC/JTAG |

PSRAMを使うprofileは`PSRAM=enabled`を加える。
P4Xを`USBMode=default`でbuildするとSerialがUART0になり、制御用のUSB Serial/JTAGにlogが出ない。
`USBMode=hwcdc`は制御/logの経路の選択で、試験データには使わない。試験対象はEspUsbDeviceのHS controllerと、相手のEspUsbHostのHIGH_SPEED port。

## P4とP4Xの違い

製品のsourceにP4とP4Xを区別する処理はない。違いはbuild設定とSDKにある。

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

- 上表はインストール済みCore 3.3.12のboards.txt、両SDKのsdkconfig、hw_ver1/3の`soc/usb_dwc_cfg.h`を比べた値。clockはbuild設定で、実測ではない。
- menuの表示は「v3.00 or newer」だが、配布SDKの最小revisionは3.1。v3.0の実機は対応済みとして扱わない。
- endpoint数とFIFO容量は同じなので、製品側で変える根拠はない。
- 実機で見えた差：Device側のGOTGCTL BSesVldの読み値が違う（旧P4は常に0）。製品の動作には影響しない（[下記](#hs直結の列挙とvbus2026-10-31)）。

P4Xは名前だけの変更ではない。公式のv3.x guideは、CPU、memory/DMA、USB HSの設定、PHY制御の変更を挙げている。
旧P4での成功をそのままP4Xの合格にはせず、P4X実機でも試験した。
v3.xにはIDF 5.5.3以降（または6.0以降）でrebuildが必要。PCNはv1.3→v3.2への移行を推奨し、型番末尾のX、pin54の変更、16 MB品のPSRAM調達仕様の差を記載している。

- [公式v3.x User Guide](https://documentation.espressif.com/esp32-p4-chip-revision-v3.x_user_guide_en.html)
- [公式PCN202600801](https://documentation.espressif.com/en/PCN202600801_ESP32-P4_Chip_Revision_v3.2_Upgrade_Chip_Revision_v1.3_Demand_Collection_and_EOL_Plan_Description.html)
- [公式P4X board guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4x-function-ev-board/user_guide.html)（HS/FS/Serial-JTAGのconnectorの区別）

## HSで変わる値

- **MPSに縛られるもの**：HSのbulk MPSは512。MSCのstaging（transferBytes）はMPSの倍数でないと、CCIDのmessageBytesはMPS未満だと、`begin()`がESP_ERR_INVALID_SIZEを返す（設計どおり。HSではMSCの768も拒否）。HS対応controllerでは、linkがFSでもMPS 512を広告できるようにstagingを予約する。借用RXの容量もMPSの倍数が必要。CDC/Vendor/MIDIのtransferBytesは、MPS未満でも受け付ける。
- **endpoint数と周期転送**：非control IN endpointの上限は、S3が4、P4 HSが7。HIDのinterrupt endpointはHSで1024 byteまで使える。そのためHost側では、周期OUT FIFOの既定（512 B）が足りない場合がある。
- **Host側（ESP-IDF）の制約**：bulk INのnum_bytesはMPSの倍数が必要。`usb_host_endpoint_clear`はhost側のdata toggleを戻さないので、halt試験はclear前のpacket数が偶数になるように組む。P4のHCDはhostのdata bufferを自分でcache同期する。

## HS直結の結果

### HS直結の列挙とVBUS（2026-10-31）

probe `validation/p4_hs/link`。両comboとも1件ずつ成功した（旧P4 Host 24.15秒、P4X Host 83.57秒。後者は初回build込み）。
各向きでvbusSensingを無効/有効にして2回ずつ開始し、Hostは1209:0002をspeed=2（HS）で読んだ。
Device側のGOTGCTL BSesVld（bit19）は、Hostを開始する前から次の値だった。

| Device | vbusSensing有効時のBSesVld | 列挙 |
| --- | --- | --- |
| P4X（2-4） | 1（Host開始前から） | HS |
| 旧P4（2-3） | 0（Host開始後も） | HS（BSesVld=0のままでも成立） |

旧P4ではBSesVldがVBUSの有無を示さない。そこでPPPSで片方のhub portだけを切り、反対側をHostにして確かめた（2026-10-31）。
Device側はNVSの自動開始で起動し、確認後に自動開始を消した。

| 切ったport | Host | 切った側の列挙 |
| --- | --- | --- |
| 3-1.2:3（2-3 旧P4） | 2-4 P4X | できない（電源を戻すと1209:0002 HSで再び列挙） |
| 3-1.2:4（2-4 P4X） | 2-3 旧P4 | できる（1209:0002 HS） |

2-3のHS口からの電源で2-4が動く。2-3を切った向きで列挙できなかった原因（2-4側のVBUS出力の有無、2-3側の起動）は未確認で、
ユーザーの判断で追加調査はしない（2026-10-31）。
確認手順はscratchのscriptで行い、標準試験には含めない。peer_deviceの`autostart`命令はその手順のためのもの。
P4X側で見えたVBUSも、Host側のsoftwareが出したものではない（BSesVldはHost開始前から1）。
逆向きcomboもこの結線で列挙できたので、両方向の試験を有効にした。

### Vendor：列挙、コピー、借用転送（2026-10-31）

probe `validation/peer/vendor_link`。両comboで成功した：旧P4 Hostは69.29秒、P4X Hostは126.54秒（どちらも初回build込み、p4_hs/linkとの2件）。
Hostは公開EspUsbHost 2.10.1のHIGH_SPEED portを使い、Vendor INはcache準備済みのread queueとcallbackで受ける。各試験で次を3回繰り返す。

- 列挙：Hostでspeed=2、bulk IN/OUTのMPS 512。DeviceにはMPSを読む公開APIがないので、wire上のdescriptorで判定する。
- コピー方式のecho：1/511/512/513/4095/4096/4097 byte。Device側でecho量とbad=0を確かめる。
- 借用RX→借用TX：1/511/512/513/4097/65535/65536/65537 byte。完了は1回で、status・実長・buffer・idが一致し、DMA領域の外のguardが残る。RXはMPSの倍数へ切り上げて渡す。
- 借用RXの取消し：cancelDirectRxで完了が1回だけ、status=cancelled、idが一致。
- 未完了の借用RXを持ったままend()：Stoppedで1回だけ返る。今回はend()の中で返っていた（at_end=1）が、試験はそこまで要求しない。

ここでOUT DMAのcache書戻し漏れを見つけた（[下記](#見つけて直した製品の不具合)の1）。

### HS専用のpeer試験

`validation/peer/`にHS直結用のprobeを置いている。どれも`p4_p4x_hs`と`p4x_p4_hs`の両方で成功した。

| probe | 内容 |
| --- | --- |
| vendor_link | 上記 |
| msc_midi | RAM disk（sector 511/512）の書込み・読出しとDevice側の保存内容、staging 64の拒否と512での回復、2 cable MIDI echo |
| cdc_hid | CDC ACMのecho（1〜4097 byte、line coding）とboot keyboardの複合構成 |
| ccid | 模擬card：ATR、APDUとEscapeのpacket境界越え、slot状態 |
| dfu | RAM backendのDFU：EP0 download、manifest、abort、runtime detach（Host側はSDKのraw API） |
| psram_link | MSC保存先とVendor ringをDevice側のPSRAMに置いた往復（[PSRAM](#psram)） |

## S3 peer試験をHS直結へ広げた結果（2026-10-31）

新しいpeer probeを書き足す代わりに、既存S3 peer試験39本のsketch.yamlへP4/P4Xのprofileを加え、HS comboでも収集した。
P4ではEspUsbController::AutoとHostの既定portがHS controllerを選ぶので、sketchの役割は変えずに動く。
FS前提だった値は`validation/s3/common/probe_link.h`の`PROBE_BULK_MPS`から導き、S3では従来の数値のままにした。
testでは`"_hs_" in request.node.name`で同じ値を使う。conftestや独自lockは追加していない。

| combo | 結果 | 時間 |
| --- | --- | --- |
| s3（FS回帰） | 51 passed | 34分22秒 |
| p4_p4x_hs（旧P4 Host→P4X Device） | 59 passed | 40分31秒 |
| p4x_p4_hs（P4X Host→旧P4 Device） | 58 passed、1 failed（下記のISO OUT） | 66分38秒 |

修正後に関連試験を再実行した結果は次のとおり。HS両方向のaudio/videoは各17件、S3のaudioは9件、旧P4 loopbackのaudioは4件が成功した。
audio_overflowは、修正前は旧P4 Deviceで5回中2回失敗していた。修正後は10回連続で成功した。

### 見つけて直した製品の不具合

1. **OUT DMA開始時のcache書戻し漏れ**（09470d6）。P4X Deviceで、65535 byteの借用RXが9216 byte以降で0xa5のまま残った。完了は成功・実長65535と報告していた。
   - 原因：OUT DMAの開始時に、bufferのdirtyなcache行を書き戻していなかった。DMAの途中で行が追い出されると、受信データの上に書き戻される。試験の中で書戻しを足すと3回とも成功したので、原因を確定した。
   - 修正：OUT armでもcleanする。FS（旧loopback）では表に出ていなかった。P4/P4_PSRAM/P4X-PC/HS直結の全32件で回帰を確認した。S3はcache同期が空の実装なので影響しない。
2. **ISO OUTの取りこぼし**（aade7e0）。DWC2のISO OUTは、armするときに「次のmicroframe」の偶奇を指定する。Hostが1 intervalを空けて送ると偶奇が合わず、incomplete ISO OUTになる。従来はendpointを止めてFAILEDを返し、workerがarmし直していた。HSでは間に合わず、次のpacketも失っていた（届いたframeが1 packetおきに欠ける形）。
   - 修正：ISRの中で、同じDMA bufferのまま偶奇を反転してarmし直す。OUTには古いデータを送る問題がないので回数の上限は設けない。
   - 測定：連続200 packetの受信率は、修正前から両方向・S3とも200/200だった（`test_iso_out_rate`）。欠けるのは、Hostが間を空けたときだけ。

### CCIDの既定messageBytes（2026-10-31に決定）

以前の既定320は、HSでは`begin()`で必ず拒否されていた。ユーザーの決定とArduinoCore-CH32RVとの相談により、既定0を自動とした。
自動の値はbeginのときに`max(271, Bulk-OUT MPS)`に決まる（FS 271、HS 512）。明示値がMPS未満なら、従来どおり拒否する。
ccid_streamのmetadataで、容量とdescriptorのdwMaxCCIDMessageLengthを確認した（S3 271、HS 512）。
[CH32RVとの調整](V3_CH32RV_API_ALIGNMENT.ja.md)、[CCID API](V3_CCID_API.ja.md)。

## PSRAM

製品はPSRAMをCPUが読み書きする保存先として使える。USBのDMAが触るstagingは内部RAMのまま。
借用転送（readDirect/writeDirect）のbufferはDMA可能な内部RAMが必要で、PSRAMを渡すと`NotDmaCapable`で拒否する。

| 試験 | 場所 | 内容 | 結果 |
| --- | --- | --- | --- |
| 容量 | `validation/p4_hs/psram` | 初期化済み容量、外部heap、外部heapの最大blockの全域に書いて読み戻す | 4台とも33554432 B、全域一致 |
| FS loopback | `validation/p4/psram`（combo `p4_psram`） | MSC保存先（64 block）とVendor RX/TX ring（各8 KiB）をPSRAMに置く。両controller | 2件成功（53bcfeb、82.35秒） |
| HS直結 | `validation/peer/psram_link`（両HS combo） | Device側のMSC保存先4 MiB（8192 block）とVendor RX/TX ring（各8 KiB）をPSRAMに置く | 両comboで成功（P4X Device：p4_hs/psramと2件143.30秒、旧P4 Device：同111.73秒。いずれも初回build込み） |

FS loopbackの詳細：両controllerで各2回開始。MSCは各方向84 block/43008 byte、Vendorは160往復/各方向153664 byte。
ring容量8192 byteを跨いで折り返す量を流し、各領域の容量外guardとheap整合性を確認した。外部RAMのpointerであることも検査した。実測CPU 360 MHz。
記録はvalidation/.pytest-results/p4-psram-{initial,native-audit}*、p4x-psram-build.txt。

HS直結の詳細：Hostが7/7/64/7 blockを4か所（最後はLBA 8185〜8191）に書いて読み戻し、DeviceはPSRAM上の全8192 blockを期待値と照合する。
Vendorは1〜32768 byteの10種を4回echoする。8 KiBのringより大きい転送では、ringが満ちてもechoが待つだけで欠けないことを見る。
各回の後に、PSRAM上の容量外guardとheap整合性を確かめる。

## FS loopbackでの結果（2026-10-30まで）

旧P4 1台（2-1）のHS controllerとFS controllerを結線し、片方をHost、もう片方をDeviceにした試験。linkはFS。
P4は通常28件＋PSRAM 2件の計30件を、各単位で成功した。全10 classの基本通信、両controller、再開始を確認した。製品sourceは80e8676（当時）。
詳細と制約は[P4記録](../validation/p4/README.ja.md)。

| 追加分 | probe | 結果 |
| --- | --- | --- |
| Vendor/CDC/HIDと65537 byteまでのDMA | f5c69dd | 6件67.84秒 |
| MSC＋MIDI | 0bf46dd | 4件31.85秒 |
| PSRAM | 53bcfeb | 2件82.35秒 |
| Audio（UAC1/UAC2 duplex） | e577698 | 4件33.95秒 |
| CCID/Video/DFU/NCM | ee12cc1/2a7c4b0/2eb53b0/9059f36 | 4/4/2/4件 |

MSC＋MIDI：
- RAM MSCはsector 511/512で、3領域×7 blockの書込み・読出し・Device保存先を照合した。24 write＋24 read、各方向85932 byte、容量外guardを保持。
- HS対応controllerでstaging 64を指定すると拒否されることを2回確認。512へ直すとbegin/列挙/通信が回復した。
- MIDIは2 cableの32 event echoを8開始で確認（256往復）。同時trafficの性能測定ではない。
- 初回は試験側がHS controllerへstaging 64を指定した構成誤りだった。製品は変えず、拒否と再設定の検査に直した。
- 記録はvalidation/.pytest-resultsのp4-classes-{initial,after}*とp4x-compile-audit.json。

Audio ISO：
- UAC1/UAC2×両controllerの4件。各caseで2回開始し、Host→Deviceへ5a、Device→Hostへ6bのPCMを同時に流した。16 kHz/mono/16 bit。
- 開始後100 msはring充填のwarmupとし、その後約350 msを照合した。8測定でplayback 89600 byte、capture 88064 byte、payload不一致なし。overrun/underrunとも0、active formatのsnapshotも一致。
- UAC1は明示feedbackなし、UAC2はfeedback更新あり/reject 0。
- 初回はUAC1にも明示feedbackを要求した試験条件で失敗した。PCMは正しく、protocolごとの契約へ試験を直した。製品の変更はない。
- 記録p4-audio-{initial,after,duplex}*、p4x-audio-compile-audit.json。

P4X実機が入る前は、postv3でのcompile、RISC-V ELF、SDK package、29 source依存、native実装のリンク先、未選択classの除去を監査していた。現在はP4X実機で試験している。

## HS試験を組んだときの方針

HS peerを加えたとき（2026-10-30〜31）に決めたこと。今後probeを足すときも同じ考え方を使う。

- primary Host＋peer_deviceの標準構造を使い、テスト関数は`dut`と`peers["device"]`を受け取る。結線で決まる組を標準comboへ列挙し、profileの直積、独自conftest、テスト本文での設備選択はしない。
- combo名はHostを先に書く。最初に旧P4 Host→P4X Deviceで確かめ、逆向きはVBUSの供給元を実機で確認してから有効にした。
- FS前提の値（MPS 64、FSのFIFO/ISO interval、S3専用register、endpoint数）は、速度や能力から期待値を決める。性能や機能を同じに見せるためのsilent fallbackはしない。FSへfallbackしたらHS試験は失敗とする。build/接続/timeoutの問題をskipで隠さない。
- Vendor INの主経路は、公開Hostのcache準備済みqueue＋callback。既知のvendorReadSyncのcache問題はHost側の別問題として扱い、HSの必須試験に混ぜない。
- 目的は動作と契約の確認。旧版とのbenchmarkや最大帯域の最適化は優先しない。
- NCMは同じMCU内のlwIP通信だけで合格にせず、raw frameとUSB転送量でwire経由を確かめる。DFUはRAM backendで確かめ、実OTA/SDへの書込みは別の試験範囲とする。
