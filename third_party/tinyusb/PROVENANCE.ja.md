# TinyUSBの由来と管理方法

[English](PROVENANCE.md)

EspUsbDevice v2は、USB設定とruntimeをArduino-ESP32のprebuilt
`libarduino_tinyusb`へ依存させないため、TinyUSB device stackの選択済みsourceを
同梱します。

- Pin metadata: `UPSTREAM.json`（repository、full commit SHA、TinyUSB version、
  確認したArduino-ESP32 baseline、選定理由）
- License: MIT
- Arduino build tree: 選択済み`.c` fileと、S2/S3/P4で必要になるtransitive
  headerだけをlibraryの`src/`以下へ配置
- Build manifest: `BUILD_FILES.txt`（43 file: source 12、header 31）
- Verification cache: 固定したupstream tarballと選択済み43 fileを、検証時だけ
  ignoredな`.upstream-cache/`以下へ取得
- 初回import時のlocal patch: なし

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

### 見直す頻度

- 対応するArduino-ESP32 baselineを変更するたびにpinを見直す。原則として、そのcoreの
  S2/S3/P4 tool packageに記録されたTinyUSB commitを優先する。
- EspUsbDeviceのmajor/minor release前にも見直す。core更新もreleaseもない場合は、
  少なくとも四半期に1回upstreamのrelease、bug fix、security情報を確認する。
  「見直し」は必ずしも「更新」を意味しない。
- それ以外で更新するのは、関係するupstream bug/security fix、必要なUSB機能、
  target対応がある場合に限る。core同梱commitから意図的に外す場合は、
  `UPSTREAM.json`の`selection_reason`へ理由を記録する。
- moving branchやversion文字列ではなく、upstreamのfull commit SHAへ固定する。

### 更新手順

1. 新しいArduino-ESP32 baselineをinstall/selectし、S2/S3/P4 tool packageそれぞれの
   `versions.txt`にある`tinyusb:`行を確認する。全targetが同じcommitとは仮定しない。
2. `UPSTREAM.json`のfull `commit`、`tinyusb_version`、
   `reviewed_with_arduino_esp32`、`selection_reason`を更新する。commitを変えるとcache
   directoryも変わるため、次のverify/update commandで新しいcommitが自動取得される。
3. `python3 tools/verify_tinyusb_vendor.py`を実行する。新しいpinではcacheを取得した後、
   選択済みfileの差分により通常は失敗する。この失敗を差分reviewの開始点とする。
4. `python3 tools/update_tinyusb_vendor.py`で反映対象fileをdry-run表示してupstream差分を
   reviewし、問題なければ`python3 tools/update_tinyusb_vendor.py --apply`を実行する。
5. S2/S3/P4の全example matrixをclean compileする。compiler dependencyが変わった場合は
   clean dependency fileから`BUILD_FILES.txt`を再生成し、新しく必要なupstream headerを
   追加し、不要になったfileを削除して、3 targetすべてが通るまでcompileを繰り返す。

   ```sh
   cd tests
   uv run --env-file .env pytest examples_compile/ --clean -vv
   ```

6. `python3 tools/verify_tinyusb_vendor.py`を再実行する。実機testへ進む前に
   byte-for-byteで成功しなければならない。
7. 完全な実機suiteを実行する。

   ```sh
   uv run --env-file .env pytest --clean
   ```

8. full diff、upstream license/SPDX header、link-map symbol audit、provenanceを確認する。
   upstream headerは変更せず、local patchを加える場合はすべてこの文書へ記録する。
   Arduino-ESP32の`esp32-hal-tinyusb` integrationはこのtreeへcopyしない。
