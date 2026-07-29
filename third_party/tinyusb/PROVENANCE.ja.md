# TinyUSBの由来と管理方法

[English](PROVENANCE.md)

EspUsbDevice v2は、USB設定とruntimeをArduino-ESP32のprebuilt
`libarduino_tinyusb`へ依存させないため、TinyUSB device stackの選択済みsourceを
同梱します。

- Upstream: https://github.com/hathach/tinyusb
- Commit: `53f8c53c2cbd73a91a172f1ae35e9abc00eb5075`
- Version macro: `0.21.0`
- License: MIT
- Arduino build tree: 選択済み`.c` fileと、S2/S3/P4で必要になるtransitive
  headerだけをlibraryの`src/`以下へ配置
- Build manifest: `BUILD_FILES.txt`（43 file: source 12、header 31）
- Verification cache: 固定したupstream tarballと選択済み43 fileを、検証時だけ
  ignoredな`.upstream-cache/`以下へ取得
- 初回import時のlocal patch: なし

このcommitはArduino-ESP32 3.3.11のS2、S3、P4 tool packageに記録されている
TinyUSB commitと同じです。

```text
tinyusb: master 53f8c53c2
```

検証用cacheはcanonical upstreamである
[`hathach/tinyusb`](https://github.com/hathach/tinyusb)から取得します。
[`espressif/tinyusb`](https://github.com/espressif/tinyusb)はそのforkであり、この
Git commitを同じSHAで保持しています。full commit SHAが同じならGit treeも同じなので、
選択したsource内容に差はありません。取得元にはcanonical upstreamを使用します。

## 初期build source

Core:

- `src/tusb.c`
- `src/common/tusb_fifo.c`
- `src/device/usbd.c`

Class:

- `src/class/hid/hid_device.c`
- `src/class/cdc/cdc_device.c`
- `src/class/midi/midi_device.c`
- `src/class/msc/msc_device.c`
- `src/class/vendor/vendor_device.c`
- `src/class/net/ncm_device.c`
- `src/class/audio/audio_device.c`

ESP32 DWC2 device controller:

- `src/portable/synopsys/dwc2/dcd_dwc2.c`
- `src/portable/synopsys/dwc2/dwc2_common.c`

完全なupstream source treeはこのrepositoryで管理しません。検証scriptは、固定commitの
tarballがignored local cacheにない場合だけdownloadし、manifestにある43 fileだけを
展開してArduino build treeとbyte-for-byteで比較します。通常のArduino buildは何も
downloadしません。

Arduino build treeは、S2/S3/P4のclean buildで生成したcompiler dependencyから求めた
最小構成です。Host、Type-C、DFU、Video、Printer、MTP、MIDI 2.0、ECM/RNDIS、
FreeRTOS以外のOSAL、ESP32以外のportable fileはbuild treeへコピーしません。

`src/`内のTinyUSB build treeは固定snapshotからの機械的なcopyで、upstream fileへ
patchを加えていません。`src/tusb_config.h`と
`src/internal/EspUsbTinyUsbConfig.h`はEspUsbDevice独自のintegration fileです。

## 更新ルール

- moving branchやversion文字列ではなく、upstreamのfull commit SHAへ固定する。
- upstreamのcopyright / SPDX headerを変更しない。
- local patchを加える場合は、すべてこの文書へ記録する。
- `python3 tools/verify_tinyusb_vendor.py`を実行し、Arduino build treeが
  `BUILD_FILES.txt`と一致すること、固定upstream commitとbyte-identicalであること、
  選択したsource/header以外を含まないことを確認する。
- local verification cacheを破棄して再取得する場合は
  `python3 tools/verify_tinyusb_vendor.py --refresh`を使用する。
- TinyUSB commit、enableするclass、対応targetのいずれかを変更した場合は、S2/S3/P4の
  clean compiler dependencyからbuild manifestを再生成する。
- 更新を受け入れる前にhost descriptor test、S2/S3/P4 compile test、link-map symbol
  audit、完全な実機pytest suiteを実行する。
- Arduino-ESP32の`esp32-hal-tinyusb` integrationはこのtreeへcopyしない。
