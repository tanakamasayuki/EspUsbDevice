# TinyUSBの由来と暫定依存

[English](PROVENANCE.md)

v3では全USB classを独自driverへ移行した。TinyUSBのclass実装10本と関連headerを
Arduino build treeから削除し、同じ場所へ別のvendor snapshotを重ねない。
共通EP0/coreも独自実装へ移行し、旧usbd.c・bootstrap・FIFO実装を除去した。
controller境界は独自`src/internal/EspUsbController.h`へ移した。
DWC2と一部wire/protocol定義は暫定依存として残る。
最終方針はこれらも置換することであり、この削除だけでTinyUSB依存撤去完了とはしない。

- Pinと選定理由: `UPSTREAM.json`。full commit SHAへ固定する。
- License: MIT、`LICENSE`と残存fileのcopyright/SPDX表示を保持する。
- 現在の選択: `BUILD_FILES.txt`の12 file（2 source、10 header）。
- 残るsource: `portable/synopsys/dwc2/dcd_dwc2.c`、`portable/synopsys/dwc2/dwc2_common.c`。
- vendor class headerは残さない。必要なUS keyboard table行だけを
  `src/keymap/keymap_en_us.h`へMIT帰属付きで移し、HIDの未使用定義やtemplateを撤去した。
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

- 削除したDCD/private、FIFO、OS抽象、umbrella headerはGit履歴だけに残す。
  独自controller event layoutはTinyUSB `device/dcd.h`由来で、MIT帰属表示を維持する。
- `portable/synopsys/dwc2/dcd_dwc2.c`: STALLしない停止と、GHWCFG3の実カウンタ幅に基づくDMA分割。
  S3のpacket数は7 bitであり、共通register型の10 bitとは異なる。
  中間armでは完了eventを出さず、short/ZLPまたは全体の完了で一度返す。

- DWC2のstate/SETUP storageを独自coreの開始時領域へ渡す暫定hook。
  static転送配列をdescriptor maskごとのrecordへ置換し、停止後に解放する。
  `src/internal/EspUsbDwc2Runtime.h`はfirst-partyの内部境界で、upstream fileには含めない。

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

SETUP領域は初回DMA前にclean/invalidateし、同期成功後にruntime pointerを公開する。失敗時は未公開の領域をcoreへ返す。

DWC2の構成解除ではDMA停止完了を待ち、latched非制御IRQを消す。endpoint ISRとtask操作を同じlockへ揃えるpatchも記録する。SOF class callbackはlock外に保つ。


controller境界は型付きruntime optionとdevice専用init、`espusb_controller_*` symbolを使う。
ESP adapterは従来のinternal DMAへ限定し、未使用のslave/FIFO経路とrecord内のFIFO pointerを削除した。
coreはqueue/fence容量とFreeRTOS同期を直接所有する。updaterは削除したDCD/private、OS/FIFO、
umbrella headerを復元しない。DWC2 register/port実装とwire/protocol定義は上流由来のままで、
この境界移行をDWC2完全置換とは扱わない。


native bus/control/endpointは`espusb_device_*` symbolを使い、旧`tud_*`/`usbd_*`入口をexportしない。
未使用の旧bootstrap・task pump・user SOF hookを撤去し、選択classのSOF callbackはcontroller eventで維持する。
CDC/MIDIのbyte layoutは旧TinyUSB `device/usbd.h`のtemplateから移行した
（Copyright (c) 2019 Ha Thach、MIT、本directoryにlicenseを保持）。
MIDIはruntime cable graphと長さを直接生成し、対称/単一cable macroの結果を上書きしない。
wire typeとDWC2 sourceの完全置換は別途継続する。


native core/classは独自`src/internal/EspUsbWire.h`の要求・interface・endpoint型、
raw byte属性とLE読み取り、native speed/resultを使う。vendor型へのaliasは作らない。
残るDWC2 adapterも境界では同型を使うが、内部のregister/common helperは上流由来のまま。
US key tableはTinyUSB HID_KEYCODE_TO_ASCII由来（Copyright (c) 2019 Ha Thach、MIT）。
VBUS policyは`EspUsbDeviceConfig::vbusSensing`からruntimeで渡し、旧build flagは拒否する。
