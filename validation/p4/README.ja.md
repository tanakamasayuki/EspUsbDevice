# P4実機loopback

Device製品source `80e8676`、probe `f5c69dd`。
既存P4（port alias `esp32-p4-e8f60ae0aa24`）のnative HS/FS connector間の結線を使う。
書き込み・log portはdata cableと独立。Core3.3.12、公開EspUsbHost2.10.1、local EspUsbDevice v3。

| Device側 | Host側 | 実際のlink |
| --- | --- | --- |
| HS controller / UTMI | FS controller | FS、12 Mbps、bulk MPS64 |
| FS controller | HS controller / UTMI | FS、12 Mbps、bulk MPS64 |

両controllerのDevice動作を確認したが、**480 MbpsのHS link試験ではない**。

## 結果

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
DMA loanは64-byte alignedのinternal RAM。PSRAM、任意の全競合、全class、HS帯域は未検証。
RXは投稿容量全体をDMAへ貸すため、guardは実受信長ではなく投稿容量の外側へ置く。

```sh
cd validation
uv run --locked --env-file .env pytest p4 --combo p4 -q -rs
uv run --locked --env-file .env pytest --combo all --collect-only -q
```

標準allではS3の47件＋P4の6件＝53件を、登録した結線に対応するcomboだけで収集する。
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
