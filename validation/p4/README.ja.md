# P4実機loopback

Device製品source `80e8676`。現在の結果は次表、初期probeの詳細は後段を参照。
既存P4（port alias `esp32-p4-e8f60ae0aa24`）のnative HS/FS connector間の結線を使う。
書き込み・log portはdata cableと独立。Core3.3.12、公開EspUsbHost2.10.1、local EspUsbDevice v3。

| Device側 | Host側 | 実際のlink |
| --- | --- | --- |
| HS controller / UTMI | FS controller | FS、12 Mbps、bulk MPS64 |
| FS controller | HS controller / UTMI | FS、12 Mbps、bulk MPS64 |

両controllerのDevice動作を確認したが、**480 MbpsのHS link試験ではない**。

## API調整後の関連回帰

Video投稿型統一02c7509のP4 Video4件と、CDC期限付き送信22afc15のloopback6件が成功。
前者はS3も含む12件179.81秒、後者も関連12件374.48秒の一括実行に含まれる。
今回も両controllerのFS link。以前の30件を最新版で全て再実行したという意味ではない。
記録video-submission-initial*、cdc-tx-related*。標準allの現在収集はS3 50＋P4 30＝80件。

## 基準80e8676の結果（2026-10-30）

全10 classの基本的なwire通信を、両Device controller・FS linkで確認した。
製品sourceとユーザーAPIに今回の変更はない。新規probeを標準comboで実行し、conftestは追加していない。

| probe | 成功case数 | 確認内容 | 実行時間 |
| --- | --- | --- | --- |
| loopback | 6 | Vendor copy/borrowed DMA、CDC、Keyboard複合 | 67.84秒 |
| classes | 4 | MSC RAM media、2 cable MIDI | 31.85秒 |
| psram | 2 | 32 MiB実測、MSC保存先とVendor両ringを外部RAMへ | 82.35秒 |
| audio | 4 | UAC1/UAC2 ISO、同時playback/capture、feedback | 33.95秒 |
| ccid | 4 | ATR、APDU、Escape、removable/fixed-card | 32.72秒 |
| video | 4 | Bulk/ISO、frame内容とloan完了 | 28.43秒 |
| dfu | 2 | EP0 download/manifest/abort、runtime detach | 97.62秒（初回build含む） |
| ncm | 4 | NTB容量2048/4096、raw Ethernet往復、再開始 | 27.93秒 |

**30 caseを各probeの別実行で成功**。全30件一括実行の結果ではない。
標準all収集はS3 47＋P4通常28＋PSRAM2＝77件。対象の合わないcomboは収集しない。
各probeで開始・正常終了を反復し、30 serial logsにpanic/heap破損はなかった。
P4X、480 Mbps HS link、PC OS binding、実OTA/SD/codecの認定結果ではない。

### 後半4 classの詳細

- CCID（ee12cc1）: 8開始で72 APDU＋24 Escape。ATR取得・power状態と、removable/fixed-cardを照合。初回はHost APIの最小4-byte条件に合わない1-byte APDUを指定しており、probeを修正した。
- Video（2a7c4b0）: Bulk/ISO×両controller、8開始。各1/63/64/65/511/513/4097 byte、計56 frame/42512 byte。受信内容、完了status・実長・同一bufferの返却、guardを照合。synthetic MJPEG payloadであり、画像codecの妥当性は試験しない。
- DFU（2eb53b0）: RAM backendのみ。4開始で36 download block/6660 byte、manifest4回、abort・runtime detach各4回。GETSTATE/GETSTATUSとpoll時間を照合。保存先の内容/guardを確認し、実Flashを書き換えるbackendは使わない。raw SDK Host transferには呼出側のcache準備を加えた。
- NCM（9059f36）: 8開始で72 frame/各方向28504 byte、受信/送信stats一致・invalid NTB 0。IP/netifを使わないため同じMCU内のIP routingでは代替されない。送信元guard/heapも保持。Host開始/終了のerrorは全て0。

NCM初回は`networkClose()`→Device終了→Host終了という順序で最初の9往復は成功したが、
2回目のHost開始が失敗した。明示closeを省いて通常のDevice切断→Host終了へ直すと4件が成功した。
このclose併用経路は公開Host2.10.1の再開始に関する別の未解決事項として記録する。
Deviceの修正やSDK/インストール済みHostの書換えはしていない。

追加4 classのRISC-V ELF、FQBN、29 source依存、native controller/PHY出所、
未選択class stateの除去も監査した。EP0だけを使うDFUでは不要なdata endpoint openも除去される。
結果原本は`validation/.pytest-results/p4-{ccid-after,video-initial,dfu-initial,ncm-restart}*`、
依存監査は`p4-new-classes-native-audit.json`、収集一覧は`p4-final-collect.txt`。
NCM失敗原本は`p4-ncm-ready*`へ残した。

## 追加したMSC/MIDIとP4X調査

MSC＋2 cable MIDIの新規4件が31.85秒で成功。8開始、24 write/24 read、256 MIDI往復、
4 logs正常停止/panicなし。これまでの6件と合わせP4は10項目を別実行で確認した。
標準allの収集はS3 47＋P4 10＝57件。P4X用postv3 compileも成功したが、接続中2台はともにv1.3。
[差異・SDK設定・追加機材と試験計画](../../docs/V3_P4_VARIANTS_AND_BENCH.ja.md)を参照。

## PSRAMの追加確認

probe53bcfeb、PSRAM enabled profileで2件82.35秒成功。既存P4に32 MiBあり、実測CPU360 MHz。
MSC保存先とVendorの両ringを外部RAMへ置き、160往復、84 block read/write、guard/heapを確認した。
USB DMA stagingは内部RAM。PSRAMへの直接DMAという結果ではない。postv3＋PSRAM enabled compileも成功。
専用comboは`p4_psram: primary=p4_psram`。通常P4と同じportへ設定して逐次実行する。

```sh
uv run --locked --env-file .env pytest p4/psram --combo p4_psram -q -rs
```

## Audio ISOの追加確認

probe e577698、UAC1/UAC2×両controllerの4件33.95秒成功、4 logs全正常停止。
各2開始し、16 kHz/mono/16 bitのplayback/captureを同時に流した。100 msのwarmup後の
測定で89600/88064 byteを照合、bad0、playback overrun/capture underrun0。
UAC1は明示feedbackなし、UAC2はfeedback更新/reject0も確認。codec/I2Sは使わない。
postv3 Audio compileも成功。P4X実機の確認ではない。

```sh
uv run --locked --env-file .env pytest p4/audio --combo p4 -q -rs
```

Audio追加時点ではP4通常14＋PSRAM2の16項目、S3と合わせ63項目を標準allで収集した。
各単位の実行結果と区別し、全63一括成功とは扱わない。

## 初回6件の結果

標準pluginのcombo `p4: primary=p4_loopback`、独自conftestなし。
6件全成功、67.84秒、skip/error/failure/unfinished 0。6 serial全て正常停止、panicなし。
3種類×2 controllerの各caseで3回開始・停止し、最後にも停止を確認した（18開始、24停止応答）。

| 対象 | 確認したこと |
| --- | --- |
| Vendor copy | 1/63/64/65/127/128/129/511/512/513/4097 byte、132往復・各方向74520 byte |
| Vendor borrowed TX反復 | 65/129/513/4097 byteを各開始で32巡。1536完了、payload一致と元領域のguard保持 |
| borrowed RX→TX | 1/63/64/65/129/513/4097/65537 byte。48 RX＋48 TX返却、各方向422814 byte、status/実長/payload/容量外guard |
| Vendor＋CDC＋Keyboard | 3 Function同時構成。上記Vendor転送に加え、CDC 24往復/4242 byte、line coding 230400、HID 18 press/18 release |

3 Functionは同一構成で各通信を確認している。3種類のtrafficを同時に流した性能試験ではない。
DMA loanは64-byte alignedのinternal RAM。この初回6件ではPSRAM、任意の全競合、全class、HS帯域は未検証。
RXは投稿容量全体をDMAへ貸すため、guardは実受信長ではなく投稿容量の外側へ置く。

```sh
cd validation
uv run --locked --env-file .env pytest p4 --combo p4 -q -rs
uv run --locked --env-file .env pytest --combo all --collect-only -q
```

初回は標準allでS3の47件＋P4の6件＝53件を収集した。現在の件数は追加結果を参照。
S3全47件と今回P4全6件は別実行で成功した。全53件一括成功という記録ではない。
`TEST_SERIAL_PORT_P4_LOOPBACK`を設定する。他のP4やS2/S31は自動選択しない。

## Hostのcache不具合の切り分け

公開EspUsbHost2.10.1の同期`vendorReadSync()`で、P4 FS Device＋HS Hostの複合構成に
129 byteの最終1 byteが化ける事例を再現した。受付・完了・実長は成功し、offset128の値が
期待01に対して98だった。Deviceの送信元は01のまま。長さ不足やHostのpartial readではない。

Hostの他のIN経路には既存のDMA開始前cache write-backがあるが、同期Vendor経路にはなかった。
Hostの隔離worktree `../EspUsbHost-p4-cache`、branch `fix/p4-vendor-read-cache`、commit `7f25896`で
その呼出しを1か所追加。同一Device/probeでFS複合3開始・384 borrowed TXが成功した（1件192.17秒、初回build込み）。
Hostのmain、インストール済み2.10.1、Device本体は変更していない。Host修正はlocal commitのみで未公開。

通常のP4 probeは公開2.10.1のcache処理済みIN queueを使う。公開`onVendorData` callbackで、
送信前に用意した照合領域へ受信する。Host既定512-byte ringを長い往復の保存先にはしない。
追加build flag、SDK patch、未公開Hostへの恒久依存はない。
`start fs composite sync`→`exercise`は同期経路の比較用として残した。通常pytestはこの診断modeを使わない。

## 記録

`validation/.pytest-results/`（git対象外）へ保存した。

- `p4-loopback-queue*`：最終6件のJUnit/serial/transcript/probe snapshotと監査。
- `p4-final-audit.json` / `p4-final.elf` / `p4-final-nm.txt`：RISC-V ELF、FQBN、29 source依存、native出所、未選択class除外。Hostは公開2.10.1と確認。
- `p4-host-cache-after*` / `p4-host-cache-fix.patch`：隔離Hostの比較成功、当時のprofileとpatch。
- `p4-composite-{first,data}*` / `p4-loopback-stress*`：同期経路の不一致原本。
- `p4-loopback-dma*`：初回の容量内guard誤期待と、投稿容量外へ直した後の成功。
- `p4-queue-{start,ring}-before*`：probe側のqueue開始mode・受信ringの扱いを直す前の失敗原本。

最終serial原本は`/tmp/pytest-embedded/2026-10-29_15-03-47-347414/`。
