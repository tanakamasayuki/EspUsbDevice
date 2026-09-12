# 手動テスト

> English: [README.md](README.md)

手動テストは、pytest だけでは完全に制御できない挙動に限定します。
例: ホスト OS の列挙表示、LED の目視確認、外部 USB analyzer、物理的な配線変更。

**このディレクトリのファイルを `test_*.py` という名前にしてはいけません。** pytest が
収集の判断に使うのがこの接頭辞で、ここにあるものはすべて、常時つながっているとは限らない
実機か、見ている人間を必要とします。その名前を付けると、引数なしの `pytest` でも
`pytest manual/` でも拾われ、ライブラリとは無関係な理由で失敗するか止まります。
担保しているのは命名だけで、marker も `testpaths` も意図的に使っていません。理由は
[../TEST_PLAN.ja.md](../TEST_PLAN.ja.md) にあります。

実行はスクリプトを名指しで行い、収集経由では行いません。

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

## `cdc_multi_ports`（CDC 複数ポートを1本ずつ確認する）

目的:

- 複数 CDC ポートを持つ device の**全ポート**について、実際にデータが往復すること、
  そしてポート同士が独立していることを確認する。
- 自動テストでは `peer/usb_serial_multi`（S3・2 ポート）と `loopback/usb_serial_multi`
  （P4・3 ポート）が per-port で検証している。この手動テストが押さえるのは、PC の OS が
  実際にポートごとのシリアルノードを作り、そこに名前が届いているかどうか。
- あわせて、ポート名（IAD の `iFunction` / control interface の `iInterface`）がホスト側に
  届いていることも確認する。名前が無いと 2 つの ACM 機能は区別できない。

必要なもの:

- [`examples/SerialMulti/`](../../examples/SerialMulti/) を書き込んだボードと、その
  device connector をこの PC へ接続（このスケッチはポート名を前置してエコーを返すので、
  どのポートが受けたかが返信そのものから分かる）
- pyserial を利用できる PC

手順:

```
cd tests
uv run --with pyserial python manual/cdc_multi_ports/cdc_multi_ports.py
uv run --with pyserial python manual/cdc_multi_ports/cdc_multi_ports.py --expect 3
uv run --with pyserial python manual/cdc_multi_ports/cdc_multi_ports.py --pid 0x4018 --serial espusb-dualserial-0001
```

期待する結果:

- ポート数が SoC の上限どおり（S2/S3 は 2、ESP32-P4 は 3）。
- 各ポートの `name=` が `Console` / `Data Link` / `Telemetry` と表示される
  （`(unnamed)` なら iInterface が届いていない）。
- 各ポートへ送った probe の返信が、そのポート名を前置して返る。返信がポート間で
  重複しない（重複したら 2 つのノードが同じ機能を指している）。
- 最終行が `OK`。

Windows で確認する場合も同じスクリプトが動きます（COM ポート番号は `hwid` の `MI_xx`
から interface を読んで並べます）。デバイスマネージャ側では、各 COM ポートが
`iFunction` の名前で並ぶことを確認してください。

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

## `p4_hs_stream`（high-speed bulk IN の律速は何か）

目的:

- ESP32-P4 device から この PC への片方向 bulk IN のスループットを測る。パターンを
  ホスト側で検証するので、速いが取りこぼした run は良く見えない。
- それを決める 2 つの数——送信 FIFO（`CFG_TUD_VENDOR_TX_BUFSIZE`）と、その FIFO の
  どれだけを 1 回の転送が運ぶか（`CFG_TUD_VENDOR_TX_EPSIZE`）——を `build_opt.h` から
  振る。ライブラリの変更は要らない。
- 同じストリームで spin ループと `EspUsbDeviceVendor::waitWritable()` を比べる。

効くのは 2 つ目で、しかも普通は誰も触らない値です。TinyUSB の vendor class は
endpoint ごとに 1 転送しか投げず完了 callback で再 arm しますが、その転送長の既定が
bulk 1 packet です。つまり 512 byte ごとに完了割り込み・event queue・usbd タスクの
往復が入ります。ESP32-P4 rev 1.3、usbip 経由、1 run 4 MiB、9 回の median:

| FIFO | 転送長 | MB/s | 備考 |
|-----:|-------:|-----:|------|
| 512 | 512 | 9.83 | 8.33-10.21。ZLP で終了した host URB が 4〜53 |
| 8192 | 512 | 10.76 | 10.50-11.06。ZLP 終了なし |
| 8192 | 2048 | 18.64 | |
| **4096** | **4096** | **21.12** | ライブラリの ESP32-P4 既定 |
| 8192 | 8192 | 22.81 | 飽和。16384 は何も買わない |
| 32768 | 8192 | 23.34 | |

必要なもの:

- high-speed Device コネクタをこの PC に繋いだ ESP32-P4 ボード
- WSL なら Windows 側の `usbipd`
- libusb が動く PC

手順:

1. 測りたい条件を sketch と同じ場所の `build_opt.h` に書いて（空ファイルならライブラリの
   既定）書き込みます。
   ```
   cd tests/manual/p4_hs_stream
   printf -- '-DCFG_TUD_VENDOR_TX_EPSIZE=8192\n' > build_opt.h
   arduino-cli compile --profile p4_hs_stream --clean
   arduino-cli upload --profile p4_hs_stream -p <port>
   ```
   `--clean` は省略できません。Arduino は `build_opt.h` をクリーンビルドのときしか
   読み直しません。
2. WSL では Device コネクタを attach します。**次の書き込みの前に必ず detach**
   してください。書き込みはチップをリセットし、attach 中のリセットは死んだ vhci
   エントリを残して、ボードのシリアルポートまで巻き込んで固まらせます。
   ```
   usbipd.exe attach --wsl --busid <n>
   ```
3. 計測します。
   ```
   cd tests
   uv run --with pyusb python manual/p4_hs_stream/p4_hs_stream.py --runs 9
   ```

各 run はホスト側のレートと並べてデバイス自身の見え方——`write()` が拒否された回数、
`waitWritable()` が block した回数、ホストの URB が短く返った回数——を出します。最後の
ものは見る価値があります。TinyUSB は packet size の倍数の転送のあとに FIFO が空になると
ZLP を送り、それがホストの実行中 URB を早期終了させます。FIFO 512 byte のとき、遅い run は
まさにその回数が多い run です。

## `p4_hs_hid_stream`（high-speed が許すパケットサイズでの HID）

目的:

- `EspUsbDeviceHidVendor` を 511 byte report で使ったとき、high-speed の
  configuration では 512 byte の interrupt endpoint、full-speed 側では USB 2.0 の
  上限である 64 になることを確認する。
- HID report descriptor が Report Count 511 を宣言していることを確認する。ホストは
  読み出しサイズをこの値から決めます。
- 順序どおり・欠落なしで、どれだけ出るかを測る。

HID はどのホスト OS でも driver が要らない唯一のクラスで、high speed では full speed
由来の「帯域が小さいクラス」という評判は成立しません。interrupt endpoint は 125 us ごとに
最大 1024 byte を運び、しかも bulk と違ってその帯域は予約です。実測 4.03 MB/s、
7,866 report/s、欠落 0。

必要なもの: `p4_hs_stream` と同じ。深さの掃引には `libusb1`。

手順:

1. `p4_hs_hid_stream` profile で書き込み、attach します。
2. 確認します。
   ```
   cd tests
   uv run --with pyusb python manual/p4_hs_hid_stream/p4_hs_hid_stream.py
   ```

**ここで出るレートは、ホストが URB を複数 in-flight にしない限りデバイスの値ではなく
ホストの値です。** usbip 経由で同期読み 1 本ずつだと、デバイスが何をしようと約
1,100 report/s になります。ホストが 8 本以上投げると、デバイスは約 7,900/s——
1 microframe に 1 report という天井の 98%——に到達します。

## `windows_winusb`（Windows が .inf なしで WinUSB を bind するか）

目的:

- 素の vendor interface が driver package なしで Windows にインストールされること、
  すなわち compatible ID に `USB\MS_COMP_WINUSB` が出て、bind される service が
  WinUSB になることを確認する。
- 旧 descriptor 構造を強制して、置き換えた側の失敗も再現する。

Microsoft OS 2.0 の function subset を解決するのは usbccgp.sys だけで、Windows が
それを読み込むのは composite device のときだけです。したがって単一 interface の
デバイスでは、subset に包まれた compatible ID は結び付く先を持ちません。同じボード、
レイアウトのフラグ以外は同じファーム、毎回新しい device instance での実測:

| `msOs20Layout` | Status | compatible ID | service |
|---|---|---|---|
| AUTO（interface 1 本なので flat） | OK / CM_PROB_NONE | `USB\MS_COMP_WINUSB` あり | WinUSB |
| SUBSETS（従来の構造） | Error / **CM_PROB_FAILED_INSTALL** | なし | なし |

必要なもの:

- デバイスが Windows 側から見える PC（WSL なら `powershell.exe` が届くこと）。WSL では
  Device コネクタを WSL に attach していないことが条件です。

手順:

1. **この PC で一度もインストールに失敗していない serial number を選びます。**
   Windows は device instance を VID / PID / serial で識別し、失敗した driver match は
   その instance に貼り付いて二度と再評価されません。一度失敗した serial で試すと、
   いま descriptor が何を言っているかではなくキャッシュされた失敗が返ります。
   ```
   cd tests/manual/windows_winusb
   printf -- '-DWINUSB_TEST_SERIAL=\\"espusb-winusb-3\\"\n' > build_opt.h
   arduino-cli compile --profile p4_windows_winusb --clean
   arduino-cli upload --profile p4_windows_winusb -p <port>
   ```
2. Device コネクタが Windows 側にあることを確認し（WSL に attach 済みなら
   `usbipd.exe detach --busid <n>`）、Windows に聞きます。
   ```
   cd tests
   uv run python manual/windows_winusb/windows_winusb.py
   ```
3. 対照実験は `-DWINUSB_TEST_LAYOUT=2` と**別の**未使用 serial を足して、
   `CM_PROB_FAILED_INSTALL` と `USB\MS_COMP_WINUSB` の不在を確認します。

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
