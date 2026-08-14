# 手動テスト

> English: [README.md](README.md)

手動テストは、pytest だけでは完全に制御できない挙動に限定します。
例: ホスト OS の列挙表示、LED の目視確認、外部 USB analyzer、物理的な配線変更。

切り分けの手順全体は [docs/usb-device-guide.ja.md](../../docs/usb-device-guide.ja.md)
にまとめています。Device 側の Serial monitor から確認する利用者向けツールは
[`examples/Info/`](../../examples/Info/) にあります。

## `device_inspect`（Host が受け取った descriptor を表示する）

目的:

- `examples/Info/EspUsbDeviceDescriptorDump` が「送ったつもりの内容」なのに対し、
  **Host が実際に受け取った内容**を表示する。両者は byte 単位で一致するはず。
- DEVICE、CONFIGURATION（全 index）、DEVICE QUALIFIER、OTHER SPEED CONFIGURATION、
  BOS、string、HID report descriptor を、hex と block 単位の走査で表示する。
- どの speed で列挙されたか、どの kernel driver が bind したかを確認する。

必要なもの:

- EspUsbDevice のスケッチを書き込んだボードと、その device connector をこの PC へ接続
- libusb を利用できる PC

手順:

```
cd tests
uv run --with pyusb python manual/device_inspect/device_inspect.py
uv run --with pyusb python manual/device_inspect/device_inspect.py --pid 0x4051
```

`--pid` を省略すると VID `0x303a` の全デバイスを表示します。`--json` を付けると
機械可読な形式になるので、変更前後の差分を取れます。

```
uv run --with pyusb python manual/device_inspect/device_inspect.py --json > before.json
# descriptor を変更して書き込み直す
uv run --with pyusb python manual/device_inspect/device_inspect.py --json > after.json
diff -u before.json after.json
```

期待:

- 表示される CONFIGURATION の hex が、Device 側 `DescriptorDump` の出力と一致する。
- Device Qualifier と Other Speed Configuration は HighSpeed 動作時のみ返る。
- BOS は WebUSB を有効にしたスケッチでのみ返る。

注意:

- HID report descriptor の取得は Linux では usbhid の detach が必要で、Windows の
  HID driver は要求自体を通しません。取得できない場合はその理由を表示して続行します。
  取得を試みない場合は `--no-hid` を付けます。
- `Access denied` になった場合の対処は下の `p4_hs_bulk` の項と同じです（VID/PID を
  対象のものへ読み替えてください）。

## `enumeration_soak`（再列挙に耐えるか）

目的:

- 一度列挙できることと、使い続けられることは別なので、再列挙と configuration 切り替えを
  繰り返しても descriptor が変化しないこと、応答しなくならないことを確認する。

2 種類の cycle があり、通る経路が違います。

- `config`: `SET_CONFIGURATION 0` → `1`。address は保ったまま class endpoint を
  張り直し、`onBusDetached()` / `onBusAttached()` を発火させる。**deconfigure を
  跨いで残った class 側の状態**を捕まえます。
- `reset`: 実際の USB port reset。再アドレス付与と再列挙が起き、descriptor が
  組み立て直されて再送される。**初回しか正しくない descriptor buffer** や、reset を
  越えられない controller を捕まえます。

手順:

```
cd tests
uv run --with pyusb python manual/enumeration_soak/enumeration_soak.py --cycles 50
uv run --with pyusb python manual/enumeration_soak/enumeration_soak.py --mode reset --cycles 50
```

`--mode` の既定は `both`（交互）です。reset 後にデバイスが戻るまでの待ち時間は
`--settle-s`（既定 10 秒）で調整します。

期待:

- 全 cycle が `ok` で、最後に `PASS <n> cycles, descriptors identical throughout`。
- descriptor の hex と link speed が初回と変わらない。

注意:

- 失敗した cycle は理由（descriptor の差分、timeout、戻ってこない）を表示して続行し、
  最後に非 0 で終了します。
- reset は Host 側の driver を rebind させるので、対象を MSC などで mount した状態では
  実行しないでください。

## `p4_hs_bulk`（ESP32-P4 High-Speed Device）

目的:

- ESP32-P4のHS Device controllerをPCへ直結し、USB High-Speed
  （480 Mbit/s signaling）で列挙することを確認する。
- active HS configurationのbulk endpointがMPS 512、Other-Speed Configurationの
  FS bulk endpointがMPS 64であることを確認する。
- Device Qualifierを取得できることを確認する。
- raw bulk OUT/IN echoを連続実行し、timeout、短い転送、データ化けがないことを確認する。

必要なもの:

- 外部UTMI HS PHYと、そのDevice connectorを持つESP32-P4 board
- data通信対応USB cable
- libusbを利用できるPC

手順:

1. [`p4_hs_bulk/p4_hs_bulk.ino`](p4_hs_bulk/p4_hs_bulk.ino)を書き込む:
   ```
   cd tests/manual/p4_hs_bulk
   arduino-cli compile --profile esp32p4 --upload
   ```
2. Serial monitorで`P4_HS_BULK_READY`を確認する。
3. board schematicを確認し、P4の外部UTMI HS PHYへ配線されたDevice connectorをPCへ接続する。
   USB Serial/JTAG端子やGPIO26/GPIO27のFS pairではない。
4. Linuxでは任意確認として`lsusb -t`を実行し、`480M`になっていることを確認する。
5. PC側検査を実行する:
   ```
   cd tests
   uv run --with pyusb python manual/p4_hs_bulk/p4_hs_bulk.py --megabytes 16
   ```
   長時間確認する場合は、例えば`--megabytes 256`へ増やす。

Linux / WSLで`Access denied (insufficient permissions)`になった場合は、現在の接続だけ
一時的に許可して再実行できる（`001/010`はcheckerが表示した現在のnodeへ置き換える）:

```
sudo chmod a+rw /dev/bus/usb/001/010
```

恒久的にはudev ruleを追加する:

```
echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="303a", ATTR{idProduct}=="4041", MODE="0660", GROUP="plugdev"' \
  | sudo tee /etc/udev/rules.d/70-espusbdevice-p4-hs.rules
sudo udevadm control --reload-rules
sudo udevadm trigger --attr-match=idVendor=303a --attr-match=idProduct=4041
```

その後USB deviceを再接続する。WSLへusbipdで渡している場合はdetach / attachし直す。
接続ごとに`/dev/bus/usb/BBB/DDD`の番号は変わるが、udev ruleはVID/PIDへ適用される。

合格条件:

- `PASS link: USB High-Speed`。
- active descriptorのbulk IN/OUTがMPS 512。
- Device Qualifierを取得できる。
- Other-Speed Configurationのbulk IN/OUTがMPS 64。
- 指定した全byteのechoが一致し、scriptが`PASS bulk echo`で終了する。
- Device側の`P4_HS_BULK_STATUS`で`errors=0`のまま、意図しない再起動がない。

注意:

- PyUSBの実行にはlibusb backendとdevice permissionが必要。Windowsで直接実行する場合は
  WinUSB driver bindingが必要になることがある。
- 表示するMiB/sはpacketごとの同期echoを含む健全性確認値で、最大帯域benchmarkではない。
- 512-byteちょうどのechoを`flush()`するとTinyUSBは転送終端のZLPを送る。checkerは
  この正規の0-byte packetを数えて読み飛ばし、echo payload全体を比較する。
- 中断した前回実行のechoやZLPがendpoint/FIFOへ残る場合があるため、checkerは開始時に
  USB標準の`SET_CONFIGURATION 0 → 1`でclass endpointを再初期化してから比較を始める。
- HS cable/port/PHYの物理条件を含むため通常のpytestには入れず、release candidateで実行する。

## `usb_ncm`（USB CDC-NCM ネットワークデバイス）

目的:

- Host OS がボードを CDC-NCM ネットワークアダプタとして列挙し、標準 NCM ドライバを
  バインドする（ドライバインストール不要）ことを確認する。
- デバイス内蔵の DHCP サーバが host に 192.168.7.0/24 のアドレスを配ることを確認する。
- 192.168.7.1 への ping で、IP 疎通（lwIP + esp_netif + フレーム TX/RX glue）を end-to-end で確認する。

peer テストと違い、ボードの USB-OTG ポートを（peer host ボードではなく）テスト実行 PC に
つなぐ必要があるため手動です。スケッチ・`sketch.yaml`（`esp32s3` プロファイル）・pytest は
[`usb_ncm/`](usb_ncm/) にあります。

手順:

1. `usb_ncm/usb_ncm.ino` を ESP32-S3 に書き込む（または `test_usb_ncm_flash_and_enumerate` を
   実行。`esp32s3` プロファイルで書き込み、`NCM_NET 1 ip=192.168.7.1` を待つ）。
2. ボードの USB-OTG ポートを PC につなぐ。
3. host 側に 192.168.7.x のアドレスを持つ新しいネットワークインターフェースが出ることを確認。
4. ping 判定を実行:
   ```
   cd tests && uv run --env-file .env pytest manual/usb_ncm/test_usb_ncm.py::test_usb_ncm_ping
   ```
   ターゲットは `NCM_TEST_IP` で上書き可能。

期待:

- host が NCM/UsbNcm ドライバをバインド。interface class は CDC(0x02 / NCM)+ CDC-Data。
- host インターフェースが 192.168.7.x のリースを取得。
- `ping 192.168.7.1` が成功（0% loss）。
- デバイスシリアルに `NCM_NET 1 ...` が出て `rx_frames` が増える。

注意:

- デバイス側は NCM のみ（CDC-ECM は Arduino-ESP32 core で無効）。最近の Windows / macOS /
  Linux は NCM を標準対応。
- DHCP は opt-in:`net.dhcpServer(true)`（デバイスが gateway）、`net.dhcpClient(true)`
  （ブリッジした LAN からアドレス取得＝PC 側ブリッジの余地）、または `net.ipConfig(...)`
  のみ（DHCP なしの静的）。
- WSL ではデバイスのログシリアルが直接見えない場合があるが、ping テストは host の IP 疎通のみを
  必要とし、それは Windows 側 USB NIC 経由でルーティングされる。

## `examples/USBVendor`

目的:

- Host OS が vendor-specific interface を認識できることを確認する。
- bulk IN / OUT の echo が動くことを確認する。
- vendor control request に Device が応答できることを確認する。
- WebUSB BOS descriptor と landing URL が Host / browser から見えることを確認する。

手順:

1. `examples/USBVendor` を USB device 側 board に書き込む。
2. Serial monitor を開き、`USB vendor device ready` を確認する。
3. USB device port を PC に接続する。
4. Linux では `lsusb -d 303a:4019 -v` で以下を確認する。
   - `bInterfaceClass 255 Vendor Specific Class`
   - bulk OUT endpoint
   - bulk IN endpoint
   - BOS descriptor に WebUSB platform capability があること
5. libusb / WinUSB / WebUSB などの Host 側 tool から interface を claim する。
6. bulk OUT に短い byte列を送信し、bulk IN で `echo: ...` が返ることを確認する。
7. control IN request `bRequest = 0x01` を送り、`EspUsbDeviceVendor` が返ることを確認する。
8. control OUT request `bRequest = 0x02` を送り、status stage が成功することを確認する。
9. WebUSB 対応 browser で device を選択し、landing URL が期待どおり見えるか確認する。

期待:

- Serial monitor に `VENDOR_RX` と `VENDOR_CONTROL` が出る。
- Host 側で `bInterfaceClass = 0xff` の interface を開ける。
- bulk OUT の payload が bulk IN の echo と一致する。
- WebUSB URL は `example.com/espusbdevice` として返る。

注意:

- Host OS によっては kernel driver detach、permission、udev rule、WinUSB driver binding が必要。
- `EspUsbDevice` が WebUSB / Microsoft OS 2.0 descriptor を生成するが、vendor code、GUID、
  内容を差し替える API はまだ持たない。
- descriptor byte列とvendor control応答は自動テストする。実際のbrowser動作とWindows driver
  bindingはHost OS / browser / driver状態に依存するためmanualで確認する。

## `examples/MSCFatRamDisk`

目的:

- Host OS が `EspUsbDeviceMscFatRamDisk` の FAT12 RAM disk を mount できることを確認する。
- Host から `CONFIG.TXT` をコピーし、eject / unmount 後に Device 側で読めることを確認する。

手順:

1. `examples/MSCFatRamDisk` を USB device 側 board に書き込む。
2. Serial monitor を開き、`USB FAT RAM disk ready` を確認する。
3. USB device port を PC に接続する。
4. PC 側で `ESPUSB` drive が見えることを確認する。
5. drive の root に `CONFIG.TXT` をコピーする。
6. OS の eject / unmount を実行する。
7. Serial monitor に `MSC_EJECT`、`CONFIG_SIZE`、`CONFIG_BEGIN` / `CONFIG_END` が出ることを確認する。

期待:

- 初期ファイル `README.TXT` が Host 側で見える。
- `CONFIG.TXT` の内容が Serial に出る。
- eject 前に ESP32 側が file scan しない。

注意:

- RAM disk なので reset / power cycle で内容は消える。
- Host OS が format を要求した場合は、その OS が小容量 FAT12 image を mount できていない可能性がある。
- Host が書き込み中に ESP32 側で FAT を読む設計にはしない。
- 大きい firmware image の受け渡しは、この example ではなく PSRAM、SD card、または streaming update で扱う。

## `examples/MSCSdCard`

目的:

- SPI 接続の SD card を USB MSC として Host OS から読み書きできることを確認する。
- Host の eject / unmount 後に Device 側が所有権を戻せることを確認する。

手順:

1. board に合わせて `examples/MSCSdCard/MSCSdCard.ino` の `SD_CS_PIN` を変更する。
2. SD card を挿入する。内容は Host から変更されるため、必要なら backup しておく。
3. `examples/MSCSdCard` を USB device 側 board に書き込む。
4. Serial monitor を開き、`USB SD MSC ready` を確認する。
5. USB device port を PC に接続する。
6. PC 側で SD card が USB storage として見えることを確認する。
7. 小さい test file を作成、読み戻し、削除する。
8. OS の eject / unmount を実行する。
9. Serial monitor に `SD_EJECT` が出ることを確認する。

期待:

- Host から SD card の既存 FAT filesystem を mount できる。
- Host からの write が SD card に反映される。
- eject 前に ESP32 側で `SD.open()` などの file API を使わない。

注意:

- Host と ESP32 が同時に同じ SD filesystem を書くと破損しやすい。
- この example は `SD.begin()` により Arduino 側 filesystem も mount するが、MSC 所有中は file API を使わない。
- SD card socket、CS pin、SPI pin は board ごとに異なる。
