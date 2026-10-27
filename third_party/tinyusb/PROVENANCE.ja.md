# TinyUSBの由来と暫定依存

[English](PROVENANCE.md)

v3では全USB classを独自driverへ移行した。TinyUSBのclass実装10本と関連headerを
Arduino build treeから削除し、同じ場所へ別のvendor snapshotを重ねない。
共通EP0/coreも独自実装へ移行し、旧usbd.c・bootstrap・FIFO実装を除去した。
DWC2、内部C ABI/headerと一部protocol定義は暫定依存として残る。
最終方針はこれらも置換することであり、この削除だけでTinyUSB依存撤去完了とはしない。

- Pinと選定理由: `UPSTREAM.json`。full commit SHAへ固定する。
- License: MIT、`LICENSE`と残存fileのcopyright/SPDX表示を保持する。
- 現在の選択: `BUILD_FILES.txt`の24 file（2 source、22 header）。
- 残るsource: `portable/synopsys/dwc2/dcd_dwc2.c`、`portable/synopsys/dwc2/dwc2_common.c`。
- `class/`以下には現在利用するAudio/CDC/HID/MIDIのprotocol定義headerだけを残す。
  DFU/MSC/NCM/Videoの未使用定義headerも削除した。
- `src/tusb_config.h`と`src/internal/EspUsbTinyUsbConfig.h`は独自integration。
  legacy classの再有効化は拒否し、独自driverとの二重claimを防ぐ。

v2での同梱開始時は9 class、Video追加後は10 classを選択していた。
その履歴と削除したNCMのWindows用divisor patchはGitで参照できる。
独自NCMでも4-byte alignmentを用いるが、削除済みfileのpatchを現manifestへ残さない。

## 検証とlocal patch

`python3 tools/verify_tinyusb_vendor.py`はmanifest、source集合、正確なupstream内容を検証する。
canonical [`hathach/tinyusb`](https://github.com/hathach/tinyusb)の固定tarballが
ignored `.upstream-cache/`にない場合だけ必要fileを取得する。通常のArduino buildはdownloadしない。
cacheは元のsnapshotのままで、`LOCAL_PATCHES.json`の正確な置換を比較時に適用する。
更新scriptも同じmanifestとpatchを用い、削除したclassを復元しない。

残るlocal patchは暫定adapter用である。削除したusbd.cのpatchも履歴側へ移した。

- `device/dcd.h`、`device/usbd_pvt.h`: DATA PIDを保持するabort、
  queue満杯を返すFIFO fence予約、control DATAの実byte数を取得する関数。
- `portable/synopsys/dwc2/dcd_dwc2.c`: STALLしない停止と、GHWCFG3の実カウンタ幅に基づくDMA分割。
  S3のpacket数は7 bitであり、共通register型の10 bitとは異なる。
  中間armでは完了eventを出さず、short/ZLPまたは全体の完了で一度返す。

S3–S3で選択経路を検証しているが、P4/S2/S31の実機検証は未完了。
上流pinを変更する場合は残る依存に必要な修正だけをreviewし、置換元が正確に一か所へ一致することを確かめる。
由来のあるコードを独自側へ移す場合もlicenseと出所を保持する。

## 更新手順

1. Core baseline変更や関連修正が必要な場合は、各SoC tool packageの`versions.txt`と残存依存を確認する。
   moving branchではなくfull commitを`UPSTREAM.json`へ記録する。
2. `python3 tools/verify_tinyusb_vendor.py`で差分を確認する。
3. `python3 tools/update_tinyusb_vendor.py`をdry-runし、必要なら`--apply`でmanifestのfileだけを更新する。
4. 残存upstream内容とpatch、license、compiler dependency、link symbolを確認する。
5. `validation/`から標準pluginの`--combo all`で検証し、未検証のSoC/HSを区別する。
   旧`tests/`や`examples/`をv3の設計・合否基準へ戻さない。
