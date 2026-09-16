# Changelog / 変更履歴

## Unreleased

## 2.5.0
- (EN) **New class `EspUsbDeviceVideo`: a USB Video Class camera.** One control
  and one streaming interface advertising one format (MJPEG or uncompressed
  YUY2) at one frame size and rate; `sendFrame()` hands over a whole frame and
  a completion callback says when it has gone. Windows, macOS and Linux bind
  their own UVC driver with nothing to install. TinyUSB's video class is now
  vendored (`class/video/`, byte-for-byte against the pinned upstream commit,
  as `tools/verify_tinyusb_vendor.py` checks). Verified on Windows 11: 181
  frames captured, every one byte-for-byte identical to what the device sent,
  in order, at 15.16 fps against 15 advertised
  (`tests/manual/windows_uvc`). New example
  [`VideoCamera`](examples/VideoCamera/); new test
  `tests/single/video_descriptor`.
  **Bandwidth is the thing to design around**: a full-speed part has about
  1 MB/s of isochronous bandwidth, so 320x240 uncompressed runs at about 6 fps
  and MJPEG is what makes larger frames practical.
- (EN) **`begin()` now refuses a configuration whose isochronous IN endpoint
  the controller's transmit FIFO cannot back**, returning `ESP_ERR_NO_MEM`.
  Bulk and interrupt endpoints already failed visibly at enumeration when the
  FIFO ran out; isochronous ones did not. An ESP32-S3 asked for a 1023-byte
  isochronous endpoint programmed its transmit FIFO 256 words at offset 512,
  past the end of the 242-word usable area, without refusing - and then
  enumerated, bound a host driver, reported frames going out with zero
  failures, and delivered nothing, crashing tens of seconds later in an
  unrelated EP0 path. The S2 and S3 share one 1 KB FIFO between every
  endpoint, which leaves about 650 bytes for one isochronous endpoint, so
  `CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE` defaults to 512 there and 1023 on the
  P4. Audio configurations are checked by the same arithmetic.
- (EN) New `EspUsbDeviceConfig::deviceVersion`: `bcdDevice`, the device release
  number, which was fixed at 0x0100. Windows folds it into a hardware ID
  (`USB\VID_xxxx&PID_xxxx&REV_0100`) that an INF can match on. Measured on
  Windows 11: changing it updates that hardware ID on the next plug-in, keeps
  the same device instance, and triggers no driver reconfiguration. Default
  0x0100, so nothing changes unless you set it.
- (EN) **Fixed: a HID class registered after a non-HID class never worked.**
  TinyUSB asks for the report descriptor by its instance number, always 0 in
  this build, and the library used that number as a position in its
  registration table. With a vendor, CDC or MSC class registered first the
  lookup landed on that class and returned nothing, so `GET_DESCRIPTOR(Report)`
  was neither answered nor stalled and the host waited out its control timeout;
  reports went to a TinyUSB instance that does not exist. Descriptors were
  correct throughout, which is why enumeration looked fine. Measured on Windows
  11: `HidUsb` failed with Code 10 about 6 s after plugging in and, in a
  composite, the WinUSB function beside it was held behind that failure and
  never enabled its device interface. Registering the HID class first was the
  only working order. Regression guards: `tests/single/hid_registration_order`
  (lookup for every order, no host) and `tests/peer/composite_vendor_hid`
  (the host has to receive a key).
- (EN) New `EspUsbDeviceConfig::deviceInterfaceGuid`. The GUID Windows records
  as a vendor interface's `DeviceInterfaceGUIDs` was hard-coded, so every
  EspUsbDevice vendor interface in the world answered the same
  `SetupDiGetClassDevs` enumeration and a host application looking for its own
  product also found unrelated ones. It does not affect which driver binds - the
  compatible ID does that alone - only who finds the device. Must be
  `{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}`; `begin()` returns false with
  `ESP_ERR_INVALID_ARG` on anything else, because the registry property writes a
  constant length and another shape would produce a set Windows rejects with
  nothing to say why. Unset keeps the previous GUID, so existing host-side
  lookups keep working.
- (EN) The Microsoft OS 2.0 descriptor set now carries
  `MS_OS_20_FEATURE_VENDOR_REVISION`, which it never did. Windows caches the
  registry properties it read the first time it enumerated a VID/PID/serial and
  re-reads them only when that revision changes, so before this a firmware with
  a different GUID could not take effect on a PC that had already seen the
  device. It defaults to a checksum of the finished descriptor set - it moves
  whenever anything in the set moves, so there is nothing to remember to bump -
  and `EspUsbDeviceConfig::msOs20VendorRevision` overrides it for a release
  scheme of your own. `microsoftOs20VendorRevision()` reports whichever applied.
  A flat set carries it once under the set header, a subset set once per
  function subset. Descriptor sets grow by 6 bytes per function: 30/162/206
  become 36/168/218.
- (JA) **新クラス `EspUsbDeviceVideo`（USB Video Class カメラ）を追加しました。**
  control 1 本と streaming 1 本の interface で、1 つの形式（MJPEG または非圧縮
  YUY2）を 1 つのフレームサイズ・レートで宣言します。`sendFrame()` に 1 フレームを
  渡し、完了コールバックが送り終えたことを知らせます。Windows・macOS・Linux は
  いずれも標準の UVC ドライバを何もインストールせずに当てます。TinyUSB の video
  クラスを vendoring しました（`class/video/`、pin した上流コミットとバイト単位で
  一致、`tools/verify_tinyusb_vendor.py` が検証）。Windows 11 で検証済み: 181
  フレームを取得し、その全部がデバイスの送ったものとバイト単位で一致、順序も連続、
  宣言 15 fps に対し実測 15.16 fps（`tests/manual/windows_uvc`）。example
  [`VideoCamera`](examples/VideoCamera/)、テスト `tests/single/video_descriptor`
  を追加。**設計の基準になるのは帯域です。** full speed の isochronous 帯域は
  約 1 MB/s なので、320x240 の非圧縮は約 6 fps にとどまり、大きなフレームを実用に
  するのは MJPEG です。
- (JA) **コントローラの送信 FIFO で賄えない isochronous IN エンドポイントを持つ構成を
  `begin()` が拒否するようになりました**（`ESP_ERR_NO_MEM`）。bulk と interrupt は
  FIFO が尽きれば列挙の失敗として表に出ていましたが、isochronous は出ませんでした。
  ESP32-S3 に 1023 バイトの isochronous エンドポイントを要求すると、送信 FIFO は
  256 ワード＠オフセット 512（使用可能な 242 ワードの外）に、拒否されずに設定され、
  そのうえで列挙が成功し、ホストがドライバを当て、デバイスは失敗ゼロで送信したと
  報告し、何も届かず、数十秒後に無関係な EP0 の経路でクラッシュしました。S2 と S3 は
  全エンドポイントで 1 KB の FIFO を共有し、isochronous 1 本に残るのは約 650 バイト
  なので、`CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE` の既定は S2/S3 で 512、P4 で 1023 に
  しました。audio の構成も同じ計算で検査されます。
- (JA) `EspUsbDeviceConfig::deviceVersion` を追加しました。device descriptor の
  `bcdDevice`（デバイスのリリース番号）で、従来は 0x0100 固定でした。Windows は
  これを hardware ID（`USB\VID_xxxx&PID_xxxx&REV_0100`）に畳み込み、INF の照合に
  使えます。Windows 11 での実測: 変更すると次の接続で hardware ID が更新され、
  device instance は同じまま、ドライバの再構成は起きません。既定は 0x0100 なので、
  設定しなければ何も変わりません。
- (JA) **修正: 非HIDクラスの後に登録したHIDクラスが一切動きませんでした。**
  TinyUSB は report descriptor をインスタンス番号（このビルドでは常に0）で
  要求しますが、ライブラリはその番号を登録テーブルの位置として使っていました。
  vendor / CDC / MSC を先に登録すると lookup がそのクラスに当たって何も返さず、
  `GET_DESCRIPTOR(Report)` は応答も STALL もされないままホストの control
  タイムアウトまで待たされ、report は存在しない TinyUSB インスタンスへ送られて
  いました。descriptor 自体は終始正しかったので、列挙は正常に見えます。
  Windows 11 での実測は、挿してから約6秒後に `HidUsb` が Code 10 で失敗、複合
  デバイスでは隣の WinUSB function がその失敗を待たされて device interface を
  有効化しない、という症状でした。HIDクラスを先に登録する順だけが動く順でした。
  回帰ガード: `tests/single/hid_registration_order`（全登録順の lookup、ホスト
  不要）と `tests/peer/composite_vendor_hid`（ホストがキーを受け取ること）。
- (JA) `EspUsbDeviceConfig::deviceInterfaceGuid` を追加しました。vendor
  interface の `DeviceInterfaceGUIDs` として Windows が記録する GUID が直書きで、
  世界中の EspUsbDevice 製 vendor interface が同じ `SetupDiGetClassDevs` の列挙に
  応答していました。自分の製品を探す host アプリが無関係なデバイスまで拾う状態です。
  どのドライバが当たるかには影響しません（それは compatible ID だけで決まります）。
  決まるのは「誰が見つけるか」です。形式は
  `{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}` で、それ以外は `begin()` が
  `ESP_ERR_INVALID_ARG` で false を返します。registry property が長さを固定値で
  書くため、形式が違うと Windows が拒否する set ができ、しかも理由がどこにも
  出ないからです。未指定なら従来の GUID のままなので、既存の host 側の検索は
  そのまま動きます。
- (JA) Microsoft OS 2.0 の descriptor set が `MS_OS_20_FEATURE_VENDOR_REVISION`
  を出すようになりました。従来は出していませんでした。Windows は VID/PID/serial
  ごとに最初の列挙で読んだ registry property をキャッシュし、この revision が
  変わったときだけ読み直すため、これが無いと GUID を変えた firmware は「一度その
  デバイスを見た PC」では効きませんでした。既定では完成した descriptor set の
  チェックサムから導出します（set の中身が変われば動くので、上げ忘れる余地が
  ありません）。独自のリリース番号に合わせたいときは
  `EspUsbDeviceConfig::msOs20VendorRevision` で上書きでき、実際に使われた値は
  `microsoftOs20VendorRevision()` が返します。flat な set では set header の直下に
  1 つ、subset 構成では function subset ごとに 1 つ入ります。descriptor set は
  function あたり 6 byte 増え、30／162／206 が 36／168／218 になります。
- (EN) Correct the `writeDirect()` contract: cache maintenance is **not** the
  caller's job, and 2.4.0's documentation was wrong to ask for it. TinyUSB
  cleans the buffer when it arms the transfer, and on ESP32-P4 the L1 data cache
  is shared between the two cores - one `CACHE_L1_DCACHE_*` control register,
  against one per core for the instruction caches - so that clean covers a
  producer running on either core. The wording cost a real user real
  throughput: a per-run `esp_cache_msync(..., C2M)` added to satisfy it was one
  of the two things the wch-protocols session had to remove to get a 16-channel
  stream back to full rate. No code change; the behaviour was always this.
- (JA) `writeDirect()` の契約を訂正しました。**キャッシュ操作は呼び出し側の仕事では
  ありません。** 2.4.0 のドキュメントがそれを求めていたのが誤りでした。TinyUSB は
  転送を arm するときに buffer を clean しており、ESP32-P4 の L1 データキャッシュは
  2 つの core で共有です（命令キャッシュは core ごとに 1 つずつあるのに対し、
  `CACHE_L1_DCACHE_*` の制御レジスタは 1 つだけ）。したがってその clean は、どちらの
  core で作ったデータでも対象になります。この記述は実際に損害を出していて、契約を
  満たすために入れた run ごとの `esp_cache_msync(..., C2M)` は、wch-protocols 側が
  16 ch stream を本来の速度に戻すために取り除いた 2 つのうちの 1 つでした。
  コード変更はありません。挙動は最初からこうでした。

## 2.4.0
- (EN) New opt-in direct transfer path on `EspUsbDeviceVendor`, for sketches
  that stream large blocks they already hold in memory.
  `writeDirect(buffer, length)` claims the bulk IN endpoint and arms one
  transfer on the caller's own memory, `onTxComplete(sentBytes)` reports
  completion on the usbd task, and `onRxData(data, length)` hands received
  packets over from the controller's buffer. That removes both copies the
  buffered path makes. Measured on an ESP32-P4 high-speed link, one-way bulk IN,
  same host, same data, 8 reads in flight: **42.6 MB/s against 33.9** for the
  same stream through the class FIFO at 65,024-byte stages, and 41.1 against
  33.0 at 27,136 - **+26% and +24%**. The requester measured the same firmware
  with a 1 MiB / 8-deep URB harness at 46.6-48.3 MB/s, within a few percent of
  what their own patched TinyUSB reached, which this gets to with the vendored
  tree untouched. An unaligned 27,000-byte stage streams as well as an aligned
  one - the length deliberately has no alignment rule, because a run's last
  block and a status line are short and odd and that is a normal thing to
  send.
  It needs `-DCFG_TUD_VENDOR_TXRX_BUFFERED=0` in the sketch's `build_opt.h` and
  `arduino-cli compile --clean`; `directWriteSupported()` reports the library's
  own view of the build, which is what a sketch should assert, because
  `build_opt.h` reaches the sketch through a response file and a build without
  `--clean` leaves the library compiled the old way. The vendored TinyUSB is
  untouched: `usbd_edpt_claim()` / `usbd_edpt_xfer()` were already part of the
  byte-for-byte tree. The buffer contract - 64-byte aligned address,
  DMA-capable memory, 1..65535 bytes, `esp_cache_msync(C2M)` after writing from
  another core, and ownership until the completion callback - is checked, and
  `lastDirectError()` names the reason; `Busy` means a transfer is still in
  flight, which is backpressure rather than a mistake. New tests
  `tests/single/vendor_direct`, `tests/single/vendor_direct_off` and
  `tests/peer/usb_vendor_direct`.
- (EN) `onTxComplete()` should be handed a buffer that is already full. Filling
  one inside the callback puts the cost of producing the data between a transfer
  completing and the next being armed, and the endpoint is idle for exactly that
  long: measured at 27,136-byte stages, 22.8 MB/s filling in the callback
  against 27.4 MB/s when the callback only arms. Documented in the header and in
  the advanced guide, because it is the shape a sketch naturally reaches for
  first.
- (EN) The direct path is a build flag rather than a runtime choice because of
  what the buffered build does around it: the vendor class arms a zero-length
  packet of its own after any transfer whose length is a multiple of
  `wMaxPacketSize`, and that ZLP takes the endpoint claim. Measured on ESP32-P4
  high speed with 27,136-byte stages, armed from inside `onTxComplete()` the
  application wins the claim and the stream runs; armed from any other task the
  ZLP wins, the next `writeDirect()` is refused and the stream stops dead after
  one transfer. Host and device agree on it independently - in the control run,
  the host counted 8183 short URBs and the device 8183 zero-length completions.
  A direct build does not compile that path, and then the arming task stops
  mattering (26.8 MB/s from another task, against 27.2 from the callback). So
  `writeDirect()` returns `NotSupported` on a buffered build rather than working
  in a way that depends on which task called it.
- (JA) `EspUsbDeviceVendor` に opt-in の direct 転送経路を追加しました。すでにメモリ上に
  ある大きな block を流すスケッチ向けです。`writeDirect(buffer, length)` が bulk IN
  endpoint を claim して**呼び出し側自身のメモリ**で転送を 1 本 arm し、
  `onTxComplete(sentBytes)` が usbd task で完了を通知し、`onRxData(data, length)` が
  受信パケットを controller の buffer から直接渡します。buffered 経路の copy 2 回が
  どちらも無くなります。ESP32-P4 high speed・一方向 bulk IN・27,136 byte stage での
  実測は、host もデータも同じにして読みを 8 本 in flight にした条件で、同じ stream を
  class FIFO 経由で流した **33.9 MB/s に対して 42.6 MB/s**（65,024 byte stage）、
  27,136 byte stage では 33.0 に対して 41.1 で、**+26% と +24%** です。依頼元が同じ
  firmware を 1 MiB・深さ 8 の URB ハーネスで測った値は 46.6〜48.3 MB/s で、**独自に
  TinyUSB へ patch を当てた版と数 % 差**でした。このライブラリは同梱ツリーを無改変の
  ままそこへ届いています。非整列の 27,000 byte stage も整列したものと同じように流れます
  ——長さに整列の規則を置いていないのは、run の最後の block や status 行が短くて半端で、
  それを普通に送れる必要があるからです。有効化にはスケッチの `build_opt.h` に
  `-DCFG_TUD_VENDOR_TXRX_BUFFERED=0` と `arduino-cli compile --clean` が要ります。
  `directWriteSupported()` はライブラリ自身の視点を返すので、スケッチはこちらを
  assert してください。`build_opt.h` は応答ファイル経由でスケッチに届くため、
  `--clean` の無いビルドではライブラリだけ以前のままになります。同梱 TinyUSB は
  無改変で、`usbd_edpt_claim()` / `usbd_edpt_xfer()` は byte-for-byte の同梱ツリーに
  もともと含まれていたものです。buffer の契約（先頭 64 byte 整列、DMA 可能、
  1〜65535 byte、別 core で書いたら `esp_cache_msync(C2M)`、完了 callback まで所有権）は
  検査し、`lastDirectError()` が理由を返します。`Busy` は転送が in flight という意味で、
  間違いではなく backpressure です。テストは `tests/single/vendor_direct`、
  `tests/single/vendor_direct_off`、`tests/peer/usb_vendor_direct` を追加しました。
- (JA) `onTxComplete()` には「すでに埋まった buffer」を渡してください。callback の中で
  埋めると、データを作るコストが「転送完了から次の arm まで」の区間に入り、endpoint は
  その間ちょうど遊びます。27,136 byte stage での実測で、callback 内で埋めると
  22.8 MB/s、callback は arm だけなら 27.4 MB/s でした。スケッチが最初に書きたくなる形が
  一番損をするので、ヘッダと応用ガイドの両方に書いてあります。
- (JA) direct 経路を runtime ではなく build flag にしたのは、buffered ビルドの挙動の
  ためです。vendor class は、長さが `wMaxPacketSize` の倍数だった転送の完了後に自前の
  ZLP を arm し、その ZLP が endpoint の claim を取ります。ESP32-P4 high speed・
  27,136 byte stage の実測では、`onTxComplete()` の中で arm すれば application 側が
  claim を取れて stream は流れ、別の task から arm すると ZLP が勝って次の
  `writeDirect()` が拒否され、**1 本で止まります**。host と device が独立に同じものを
  数えており、対照実験では host が short URB 8183 本、device が 0 byte 完了 8183 回でした。
  direct ビルドではこの経路を compile しないので、arm する task は問題でなくなります
  （別 task から 26.8 MB/s、完了 callback から 27.2 MB/s）。したがって buffered ビルドの
  `writeDirect()` は、呼び出した task に正しさが依存する形で動くのではなく
  `NotSupported` を返します。
- (EN) Fix `EspUsbDevice::rebootToBootloader()` leaving an ESP32-S3's USB
  connector dark. The library takes the shared internal PHY for USB-OTG at
  `begin()`, and that selection lives in `RTC_CNTL_USB_CONF_REG` - an RTC-domain
  register that **survives a software reset**. The ROM's serial loader therefore
  came up with the pads routed to a controller it was not driving: measured on
  an ESP32-S3 whose only cable is its native USB, the port vanished from the
  host entirely while `esptool` over a separate UART still connected with
  `--before no-reset`. The call now hands the PHY back before restarting.
  Measured after the fix, on one connector: the sketch's own VID:PID while it
  runs, `303a:1001` (USB Serial/JTAG) after the reboot, and `esptool` then
  uploads and runs its stub flasher over that same port. The single-cable
  flashing story in `docs/ota-over-usb.md` was previously stated as something
  the hardware did by itself; it is something the library does, and it is now
  measured rather than derived. New manual test
  `tests/manual/s3_single_cable`.
- (EN) `EspUsbDevice::rebootToRomDfu()` now refuses on an ESP32-S3 whose
  `USB_PHY_SEL` eFuse is not burned, returning `ESP_ERR_NOT_SUPPORTED` without
  restarting. The ROM routes the shared PHY from that eFuse at boot whatever the
  application left behind, so its DFU stack lands on a controller with no pads:
  measured, the chip reaches the download loader and nothing at all enumerates.
  On a one-connector board, restarting anyway is the difference between a call
  that did nothing and a board that needs someone to press BOOT. The eFuse is
  one-way and costs the board USB-Serial-JTAG permanently, so the library will
  not burn it; `EspUsbDeviceDfu` implements DFU in the application instead, with
  no eFuse and the same behaviour on every target. New test
  `tests/single/rom_dfu_guard`.
- (JA) `EspUsbDevice::rebootToBootloader()` が ESP32-S3 の USB コネクタを沈黙させる
  不具合を修正しました。このライブラリは `begin()` で共有内蔵 PHY を USB-OTG 側へ
  切り替えますが、その選択は `RTC_CNTL_USB_CONF_REG`——**software reset を生き延びる**
  RTC ドメインのレジスタ——にあります。そのため ROM の serial loader は、自分が駆動して
  いない controller に pin が繋がった状態で起動していました。native USB が唯一の
  ケーブルである ESP32-S3 で実測したところ、ホストから USB ポートが完全に消え、
  別系統の UART 経由では `esptool --before no-reset` が接続しました。再起動の前に PHY を
  返すようにしています。修正後の実測は、同じコネクタで、動作中はスケッチ自身の
  VID:PID、再起動後は `303a:1001`（USB Serial/JTAG）、そしてその同じポートで `esptool` が
  stub flasher を転送・実行します。`docs/ota-over-usb.ja.md` の「ケーブル 1 本で焼ける」は
  ハードウェアが勝手にやることのように書かれていましたが、実際はライブラリがやることで、
  今回それを導出ではなく実測に置き換えました。manual test
  `tests/manual/s3_single_cable` を追加しています。
- (JA) `EspUsbDevice::rebootToRomDfu()` は、`USB_PHY_SEL` eFuse を焼いていない
  ESP32-S3 では再起動せず `ESP_ERR_NOT_SUPPORTED` を返すようにしました。ROM は
  起動時に共有 PHY をその eFuse から決めるため、application が何を残していようと
  DFU stack が pin の無い controller に載ります。実測でも、チップは download loader に
  入り、USB には何ひとつ現れませんでした。コネクタが 1 つのボードでは、それでも
  再起動することは「何もしなかった呼び出し」と「誰かが BOOT を押さないと戻らない
  ボード」の差になります。eFuse は一方向で USB-Serial-JTAG を恒久的に失うため、
  ライブラリが焼くことはしません。`EspUsbDeviceDfu` なら application 側で DFU を
  実装し、eFuse 不要で全ターゲット同じ挙動です。テストは
  `tests/single/rom_dfu_guard` を追加しました。
- (EN) Windows binds a driver to the DFU and vendor interfaces by itself, with
  no Zadig step. The Microsoft OS 2.0 descriptor set is now built for every
  interface no in-box driver claims, rather than only for a vendor interface of
  a WebUSB-enabled device, and a composite gets one function subset per such
  interface in ascending `bFirstInterface`. Measured on an ESP32-P4 against
  Windows 11, same board and same composite with only the descriptor changed: a
  DFU-only device went from `Status=Error` with no driver to `service=WINUSB`,
  and a DFU + vendor device went from the DFU child sitting on
  `problem=28` (CM_PROB_FAILED_INSTALL) to both children on `WINUSB`. The DFU
  function is given the compatible ID and no `DeviceInterfaceGUIDs`, which the
  same measurement shows is enough - libusb, and therefore `dfu-util`, finds a
  WinUSB device through the USB device interface class. `bcdUSB` now reads
  0x0201 whenever a BOS exists rather than only when WebUSB is enabled, because
  a DFU-only device publishes a Microsoft capability and no WebUSB one.
- (EN) Bulk IN endpoints get a two-packet controller transmit FIFO when there
  is room for it. The DWC2 core gives an IN endpoint one packet by default, so
  the next packet cannot be staged until the current one has left; two lets the
  controller send one while the stack fills the other. Measured on an ESP32-P4
  high-speed link, one-way bulk IN, 32 MiB per run, best of three: **22.98 ->
  28.93 MB/s**, with the run-to-run spread narrowing from ±0.7 to ±0.1. The
  cost is not RAM - the FIFO is a fixed block inside the controller - so the new
  `config.bulkInBuffering` defaults to `Auto`, which computes the budget from
  the configuration descriptor before the PHY starts and doubles every bulk IN
  endpoint only if they all fit. All or nothing per device, because a rule that
  doubled some and not others would make throughput depend on registration
  order. On ESP32-S2/S3 they always fit; on the P4 high-speed controller two
  bulk IN endpoints fit and three do not. `Single` keeps the controller
  default and `Double` demands it, failing `begin()` with
  `ESP_ERR_INVALID_SIZE` rather than starting a device whose endpoints did not
  open. `EspUsbDevice::bulkInDoubleBuffered()` reports what was applied. New
  test `tests/single/bulk_in_fifo`.
- (EN) `EspUsbDeviceVendor::waitWritable()` no longer hangs on the tail of a
  stream. TinyUSB arms a transfer only once the transmit FIFO holds a whole
  packet, so a remainder shorter than `wMaxPacketSize` sat there until
  something flushed it - and a caller blocked waiting for room is by definition
  not about to add the bytes that would round it up. It flushes before it
  waits. The same rule is why a short reply needs `flush()` while a streaming
  sketch never notices; both are now stated in the advanced guide, together
  with a plain statement that this library builds DWC2 DMA mode on every
  supported target and slave mode is not a supported configuration, and a note
  that a host which does its own work inside a libusb completion callback
  produces stalls that look like device faults.
- (EN) `docs/CHANGE_REQUESTS.ja.md` gains the second round of answers to
  [ch32-riscv-ug/wch-protocols](https://github.com/ch32-riscv-ug/wch-protocols):
  the four default-value items (D1 to D4) and the five feature requests (F1 to
  F5) raised from their E107 to E114 measurements. Every number in it was
  measured again here rather than taken from the request - which is why D1 was
  adopted and D2, raised on the same evidence, was not: the same change is worth
  26% on a bulk IN stream and nothing at all when the sketch's producer is
  trivial. The document now names its source by URL rather than by path.
- (EN) New `config.taskCoreId` pins the USB device task to a core, defaulting to
  -1 (not pinned, as before). Measured on ESP32-P4 high speed with a trivial
  producer: 28.61 MB/s unpinned against 28.90 pinned, which is no difference -
  so it stays off by default, and the advanced guide says when it is worth
  reaching for, which is when the sketch's own producer is heavy enough to
  compete with the usbd task.
- (JA) Windows が DFU / vendor interface に自分でドライバを当てるようになりました。
  Zadig は不要です。Microsoft OS 2.0 descriptor set を「WebUSB 有効な device の
  vendor interface」だけでなく「Windows 標準ドライバが当たらない全 interface」に対して
  生成し、composite では該当 interface ごとに function subset を `bFirstInterface` 昇順で
  出します。ESP32-P4 を Windows 11 に繋ぎ、同じ board・同じ composite で descriptor だけ
  変えて実測: DFU 単体は `Status=Error`（ドライバ無し）から `service=WINUSB` へ、
  DFU + vendor は DFU の子が `problem=28`（CM_PROB_FAILED_INSTALL）だったところから
  両方の子が `WINUSB` へ変わりました。DFU 側には compatible ID だけを与え
  `DeviceInterfaceGUIDs` は付けていません。同じ実測でそれで足りることが確認できています
  （libusb、つまり `dfu-util` は WinUSB device を USB device interface class で見つけます）。
  `bcdUSB` は WebUSB 有効時だけでなく BOS を出すとき常に 0x0201 になりました。DFU 単体の
  device は WebUSB capability を持たず Microsoft capability だけを持つためです。
- (JA) bulk IN endpoint の送信 FIFO を、収まるときだけ 2 パケット分にするようにしました。
  DWC2 コアは既定で IN endpoint に 1 パケット分しか与えないため、今のパケットが出るまで
  次を用意できません。2 パケット分あれば送出と充填が重なります。ESP32-P4 の high-speed
  リンク、一方向 bulk IN、1 回 32 MiB、3 回の最良で実測: **22.98 → 28.93 MB/s**。回ごとの
  ばらつきも ±0.7 から ±0.1 に縮みました。代償は RAM ではなくコントローラ内部の固定
  FIFO なので、新設の `config.bulkInBuffering` の既定を `Auto` とし、PHY 起動前に
  configuration descriptor から収支を計算して、bulk IN が全部収まるときだけ有効にします。
  device 単位で全部か無しかにしているのは、一部だけ有効にする規則だとスループットが
  function の登録順に依存するからです。ESP32-S2/S3 は常に収まり、P4 high-speed は
  bulk IN 2 本まで収まり 3 本は収まりません。`Single` はコントローラ既定のまま、`Double` は
  要求で、収まらない場合は endpoint が開かない device を起動する代わりに `begin()` が
  `ESP_ERR_INVALID_SIZE` で失敗します。適用結果は
  `EspUsbDevice::bulkInDoubleBuffered()` が返します。テスト
  `tests/single/bulk_in_fifo` を追加しました。
- (JA) `EspUsbDeviceVendor::waitWritable()` がストリームの末尾で止まらなくなりました。
  TinyUSB は送信 FIFO に 1 パケット分たまってからでないと転送を arm しないので、
  `wMaxPacketSize` 未満の端数は何かが flush するまで残ります。そして空きを待って
  止まっている呼び出し側が、その端数を切り上げるバイトを追加することはありません。
  待つ前に flush するようにしました。短い返信に `flush()` が要るのも、ストリーミング
  するスケッチが気づかないのも同じ理由で、どちらも応用ガイドに明記しました。あわせて
  「このライブラリは対応する全ターゲットで DWC2 の DMA モードをビルドし、slave は
  サポート構成ではない」ことと、「host が libusb の完了 callback 内で処理をすると
  device 側の不具合に見える停止が起きる」ことも書きました。
- (JA) `docs/CHANGE_REQUESTS.ja.md` に第 2 回の回答を追加しました。
  [ch32-riscv-ug/wch-protocols](https://github.com/ch32-riscv-ug/wch-protocols) の
  E107〜E114 から起票された既定値 4 件（D1〜D4）と機能 5 件（F1〜F5）です。数字は
  すべて依頼書から採らずこちらで測り直しており、それが D1 を採用し、同じ根拠から
  起票された D2 を採用しなかった理由でもあります。同じ変更が bulk IN ストリームでは
  26% の価値を持ち、スケッチ側の producer が軽ければ何の価値も持ちません。依頼元の
  参照はパスではなく URL にしました。
- (JA) `config.taskCoreId` を追加しました。USB device task を core に固定します。既定は
  -1（固定しない＝従来どおり）です。ESP32-P4 high speed で producer が軽い構成を実測
  すると、固定なし 28.61 MB/s に対し core 0 固定 28.90 MB/s で差がありません。よって
  既定は変えず、どういうときに手を伸ばす価値があるか（スケッチ側の producer が usbd
  task と競合するほど重いとき）を応用ガイドに書きました。
- (EN) USB firmware update. New `EspUsbDeviceDfu` implements the DFU class in
  both shapes: `Download`, where `dfu-util -D firmware.bin` writes an image into
  the spare OTA partition and the device verifies it and restarts into it, and
  `Runtime`, where `dfu-util -e` asks the device to enter the chip's ROM
  download loader. The function costs **one interface and no endpoints** -
  every DFU transfer travels on EP0 - so it can be added to a device whose
  endpoint budget is already spent; `tests/peer/usb_dfu` asserts exactly that
  against a keyboard that keeps both of its endpoints. New
  `EspUsbDeviceFirmwareUpdate` is the transport-independent OTA writer
  underneath it (`available()` / `capacity()` / `targetLabel()` before an upload
  starts, `begin()` / `write()` / `end()` to stream it in with the flash erased
  as the write advances, `markValid()` / `rollback()` / `cancelPendingBoot()`
  around the commit), usable from CDC, vendor bulk, MSC or an HTTP upload over
  the NCM interface. New `EspUsbDevice::rebootToBootloader()` and
  `rebootToRomDfu()` restart into the ROM loader from a running sketch:
  Arduino-ESP32's `usb_persist_restart()` cannot be linked from a sketch that
  uses this library (it drags in `esp32-hal-tinyusb.c`, which defines
  `tud_descriptor_bos_cb()` and `tud_vendor_control_xfer_cb()` a second time)
  and is a no-op on ESP32-P4 in any case. The download-boot flag is a different
  register per target, and on P4 shares one with the software-reset bit, so it
  must be set rather than written - verified in the download loader on ESP32-S3
  rev v0.2 and ESP32-P4 rev v1.3 with the USB stack running. TinyUSB
  `class/dfu/dfu_device.c` and `dfu_rt_device.c` joined the vendored selection
  (48 files, 14 sources). New examples `FirmwareDFU`, `FirmwareHTTP` (a browser
  uploads over the USB network interface, no host tool at all) and
  `FirmwareBootMode`; new tests `tests/single/dfu_descriptor` and
  `tests/peer/usb_dfu`; new guide `docs/ota-over-usb.md`, which covers boot mode
  per chip, entering it from a sketch, and the full route comparison. Verified
  end to end from a PC as well: an ESP32-P4 enumerated as a DFU device at high
  speed and took a 385 KB image in 376 blocks in 2.7 s (139 KiB/s) over EP0,
  then verified it, moved the boot partition and restarted into it.
- (EN) Firmware update by dragging a file onto a drive. New
  `EspUsbDeviceMscFirmwareDisk` presents a FAT12 volume whose data region **is**
  the OTA partition: the host copies a `.bin` onto it, the device recognises the
  ESP image magic and streams the sectors into flash as they arrive, then
  commits when the file's directory entry says the whole image landed or when
  the drive is ejected. The image is never held in RAM - the buffer the sketch
  supplies holds the boot sector, both FAT copies, the root directory and a
  scratch area for what a host's file manager leaves behind, which is what lets
  a board with 320 KB of RAM accept a 1.25 MB image. `begin()` picks the
  smallest cluster size from 4 KiB up that keeps the partition inside FAT12's
  4084 clusters, so a cluster boundary is also a flash erase boundary. Writes
  into the firmware region must ascend: one that jumps backwards or leaves a
  hole is refused with `ESP_ERR_INVALID_STATE` and the update abandoned, rather
  than producing an image that looks complete and is not. Reads come back from
  the partition itself, so a host that verifies what it copied sees what was
  written. New example `FirmwareMSC`; new test
  `tests/single/msc_firmware_disk`, which drives the block callbacks directly
  and covers the geometry, the published FAT, image detection, the ordering
  rule, the directory-entry commit and the verification that refuses a bad
  image. The drive also takes **UF2**, which removes the ordering rule
  altogether: each 512-byte block carries its own target address, so any write
  order works, host metadata is rejected by the block magic rather than guessed
  at, completion is exact (a seen-block bitmap, so a block written twice is not
  counted twice), and a family ID refuses an image built for a different chip
  before any flash is touched -
  `EspUsbDeviceMscFirmwareDisk::uf2FamilyId()` is what `uf2conv.py --family`
  needs. Underneath, UF2 uses new
  `EspUsbDeviceFirmwareUpdate::beginRandomAccess()` / `writeAt()`, which erase
  the image's worth of partition up front so a later write can land anywhere in
  it. Verified on hardware with eight blocks written in reverse order. New
  examples `FirmwareCDC` (a length and then the bytes, over a CDC port, with a
  Python host script) and `FirmwareVendor` (control requests for the commands,
  bulk OUT for the image - the fastest route here, with a pyusb host script).
- (JA) ドライブにファイルを放り込むファームウェア更新に対応しました。
  `EspUsbDeviceMscFirmwareDisk` は、データ領域が OTA partition **そのもの**である
  FAT12 ボリュームを提供します。host が `.bin` をコピーすると、device が ESP image の
  magic を認識して到着した sector から flash へ流し込み、ファイルの directory entry が
  示す長さに達した時点、または drive が eject された時点で commit します。イメージは
  RAM に載りません。スケッチが渡す buffer に載るのは boot sector、FAT 2 部、root
  directory、そして host のファイルマネージャが残すものを吸収するスクラッチ領域だけで、
  RAM 320KB のボードが 1.25MB のイメージを受け取れるのはこれが理由です。`begin()` は
  partition が FAT12 の 4084 cluster に収まる最小の cluster size を 4KiB 以上から選ぶので、
  cluster 境界が flash の erase 境界にもなります。firmware 領域への書き込みは昇順が
  前提で、逆戻りや穴あきは `ESP_ERR_INVALID_STATE` で拒否して更新を中止します。
  完成に見えて実は違うイメージを作らないためです。read は partition の実内容を返すので、
  コピーしたものを host が読み返しても一致します。example `FirmwareMSC`、テスト
  `tests/single/msc_firmware_disk`（block callback を直接叩いて、幾何・公開する FAT・
  イメージ検出・順序規則・directory entry による commit・不正イメージの検証拒否を
  カバー）を追加しました。このドライブは **UF2** も受け付け、その場合は順序規則が
  完全に消えます。512 byte block ごとに自分の target address を持つので書き込み順は
  任意、host のメタデータは block magic で（推測ではなく）拒否、完了判定は正確
  （「見た block」の bitmap を持つので二重書き込みを二重に数えない）、そして family ID が
  別 chip 向けのイメージを flash に触れる前に拒否します。`uf2conv.py --family` に渡す値は
  `EspUsbDeviceMscFirmwareDisk::uf2FamilyId()` が返します。内部では UF2 のときだけ
  新しい `EspUsbDeviceFirmwareUpdate::beginRandomAccess()` / `writeAt()` を使い、
  イメージ長ぶんの partition を先に erase しておくことで後続の書き込みがどこへでも
  落ちられるようにしています。block 8 個を逆順に書く実機確認済み。example
  `FirmwareCDC`（CDC port に長さ＋バイト列、Python host スクリプト付き）と
  `FirmwareVendor`（コマンドは control request、イメージは bulk OUT。ここで最速、
  pyusb host スクリプト付き）も追加しました。
- (JA) USB 経由のファームウェア更新に対応しました。`EspUsbDeviceDfu` は DFU class を
  2 形態で実装します。`Download` は `dfu-util -D firmware.bin` が空いている OTA
  partition へイメージを書き、device が検証してそのイメージで再起動するもの、
  `Runtime` は `dfu-util -e` で device にチップの ROM download loader へ入るよう
  頼むものです。消費するのは **interface 1 本と endpoint 0 本**（全転送が EP0 を
  通ります）なので、endpoint 予算を使い切った device にも足せます。
  `tests/peer/usb_dfu` が、endpoint 2 本を保ったままの keyboard の隣で実際にそれを
  確認します。その下にある `EspUsbDeviceFirmwareUpdate` は転送路に依存しない OTA
  writer で（upload 前に答える `available()` / `capacity()` / `targetLabel()`、
  書き込みの進行に合わせて flash を erase しながら流し込む `begin()` / `write()` /
  `end()`、commit 前後の `markValid()` / `rollback()` / `cancelPendingBoot()`）、
  CDC・vendor bulk・MSC・NCM 越しの HTTP アップロードからも使えます。
  `EspUsbDevice::rebootToBootloader()` と `rebootToRomDfu()` は動作中のスケッチから
  ROM loader へ再起動します。Arduino-ESP32 の `usb_persist_restart()` はこの
  ライブラリを使うスケッチからは link できず（`esp32-hal-tinyusb.c` を引き込み、
  `tud_descriptor_bos_cb()` と `tud_vendor_control_xfer_cb()` を二重定義します）、
  そもそも ESP32-P4 では何もしません。download-boot フラグのレジスタはターゲット
  ごとに違い、P4 ではソフトウェアリセットのビットと同居しているため書き込みでは
  なく set が必要です。USB stack を動かした状態の ESP32-S3 rev v0.2 と ESP32-P4
  rev v1.3 で download loader への到達を実機確認しました。TinyUSB の
  `class/dfu/dfu_device.c` と `dfu_rt_device.c` を vendoring 対象に追加しています
  （48 file、source 14）。example `FirmwareDFU` / `FirmwareHTTP`（host 側は
  ブラウザだけ）/ `FirmwareBootMode`、テスト `tests/single/dfu_descriptor` と
  `tests/peer/usb_dfu`、ガイド `docs/ota-over-usb.ja.md`（chip 別の boot mode、
  スケッチからの入り方、経路の比較）を追加しました。PC 相手の end-to-end も確認済みで、
  ESP32-P4 が high speed で DFU device として列挙され、385KB のイメージを 376 block・
  2.7 秒（139 KiB/s）で EP0 経由で受け取り、検証して boot partition を移し、
  そのイメージで再起動しました。

## 2.3.0
- (EN) Fix a composite HID device merging its classes' report descriptors at the wrong byte. The merge gives each class its own Report ID and has to put it immediately after that class's Collection (Application) item; it found that point by copying six bytes, which holds only for a descriptor opening with a one-byte Usage Page and a one-byte Usage. `EspUsbDeviceHidVendor` opens with a vendor-defined Usage Page - a three-byte item - so the cut landed *inside* the Collection item and every item after it shifted by one: the host read `A1 85` as "Collection (vendor-defined)" and the rest of the descriptor was nonsense. The device still enumerated, which is why nothing noticed. The merge now walks HID items, and a class that already declares a Report ID (gamepad, consumer control, system control, vendor HID) has it replaced rather than duplicated - those were emitting the same item twice, harmlessly but pointlessly. New `EspUsbDevice::hidReportDescriptorLength(instance)` reports the length of whatever `hidReportDescriptor(instance)` returns, which for a composite HID is the merged descriptor and not any one class's own.
- (JA) 複合 HID が各クラスの report descriptor を誤った位置で連結していた不具合を修正しました。merge は各クラスに Report ID を与え、それをそのクラスの Collection (Application) item の直後に置く必要がありますが、その位置を「先頭 6 byte をコピーする」で求めていました。これは 1 byte の Usage Page と 1 byte の Usage で始まる descriptor でしか成立しません。`EspUsbDeviceHidVendor` は vendor 定義の Usage Page（3 byte item）で始まるため、切れ目が **Collection item の内側**に落ち、以降の item が 1 byte ずつずれていました。ホストは `A1 85` を「Collection (vendor 定義)」と読み、そこから先は別物になります。デバイスは列挙してしまうので誰も気付きませんでした。merge は HID item を走査するようにし、自前で Report ID を持つクラス（gamepad / consumer control / system control / vendor HID）は重複ではなく置き換えます（従来は同じ item が 2 回出ていました。無害ですが無意味です）。`EspUsbDevice::hidReportDescriptorLength(instance)` を追加しました。`hidReportDescriptor(instance)` が返すものの長さで、複合 HID では merged descriptor の長さです。
- (EN) Fix a composite HID device dropping every report that arrives on its interrupt OUT endpoint. TinyUSB's HID driver does not parse report descriptors, so it reports such data with a report ID of 0 and the payload untouched (`class/hid/hid_device.c`); only the control SET_REPORT path knows the ID, from wValue. The composite dispatch keys on the report ID, so nothing matched and every report was discarded - a single-class device was unaffected, because it dispatches by instance. A merged descriptor declares report IDs, so the ID is the payload's first byte and is now read from there, and only when it actually names one of the merged classes. The interrupt OUT path itself could not be driven from this bench - usbip does not deliver interrupt OUT URBs (its control SET_REPORT does, confirmed through a hidraw ioctl) and EspUsbHost has no raw endpoint write - so the routing is tested by calling `handleHidSetReport()` directly in `tests/single/descriptor`.
- (JA) 複合 HID が interrupt OUT に届いた report をすべて捨てていた不具合を修正しました。TinyUSB の HID driver は report descriptor を解釈しないので、この経路のデータを report ID 0・payload そのままで上げてきます（`class/hid/hid_device.c`）。ID が分かるのは wValue を持つ control SET_REPORT の経路だけです。複合の振り分けは report ID で行うため一致するクラスが無く、すべて捨てられていました（単独クラスの device は instance で振り分けるので無事です）。merged descriptor は report ID を宣言しているので、ID は payload の先頭バイトにあります。そこから読むようにし、どのクラスの ID でもないときは何もしません。interrupt OUT 経路自体はこの台のどちらのホストからも駆動できなかったため（usbip は interrupt OUT URB を配送せず——control SET_REPORT は通ることを hidraw の ioctl で確認——EspUsbHost には生のエンドポイント書き込み API が無い）、振り分けは `tests/single/descriptor` から `handleHidSetReport()` を直接呼んで検証しています。
- (EN) `CFG_TUD_VENDOR_TX_EPSIZE` is now set on ESP32-P4, and it is the change that moves high-speed bulk IN. TinyUSB's vendor class submits one transfer per endpoint and re-arms it from the completion callback, and it sizes that transfer at a single bulk packet by default - so every 512 bytes cost a completion interrupt, an event-queue hop and a usbd task turn, about 52 us of turnaround against 46 us of wire time. DWC2 sends several packets per transfer perfectly happily; only that default stopped it. Measured on P4 rev 1.3 over usbip, 4 MiB per run, median of 9, pattern verified on the host: 9.83 MB/s at TinyUSB's 512/512, 10.76 with the FIFO alone at 8192, 18.64 at 8192/2048, 21.12 at 4096/4096, 22.81 at 8192/8192 and 22.78 at 8192/16384 - saturated. P4 therefore defaults to 4096/4096, which reaches 93% of that ceiling using 512 bytes *less* RAM than raising the FIFO alone did, and measures 21.56 MB/s from a sketch with no `build_opt.h` at all. Raising the host's URB depth past 2 adds nothing, so ~23 MB/s is this path's device-side ceiling; a second transfer in flight on the device would buy single digits. S2/S3 keep TinyUSB's transfer size, since a 64-byte full-speed bulk endpoint is not limited by this turnaround.
- (JA) ESP32-P4 で `CFG_TUD_VENDOR_TX_EPSIZE` を設定するようにしました。high-speed の bulk IN を動かすのはこの値です。TinyUSB の vendor class は endpoint ごとに 1 転送しか投げず完了 callback で再 arm しますが、その転送長の既定が bulk 1 packet です。つまり 512 byte ごとに完了割り込み・event queue・usbd タスクの往復が入り、線上時間 46 us に対して往復が約 52 us かかっていました。DWC2 は 1 転送で複数 packet を扱えます。止めていたのはこの既定値だけでした。P4 rev 1.3・usbip 経由・1 run 4 MiB・9 回の median・パターンはホスト側で検証した実測は、TinyUSB 既定の 512/512 で 9.83 MB/s、FIFO だけ 8192 にして 10.76、8192/2048 で 18.64、4096/4096 で 21.12、8192/8192 で 22.81、8192/16384 で 22.78（飽和）。したがって P4 の既定を 4096/4096 にしました。天井の 93% を、FIFO だけ上げた構成より 512 byte **少ない** RAM で出し、`build_opt.h` を一切置かない sketch で 21.56 MB/s を実測しています。ホスト側の URB 深さを 2 より増やしても変わらないので、約 23 MB/s はこの経路の device 側の天井であり、device 側に転送を 2 本 in-flight にしても残りは数 % です。S2/S3 は TinyUSB の転送長のままにしました。64 byte の full-speed bulk endpoint はこの往復では律速されないためです。
- (EN) Fix `EspUsbDeviceVendor::waitWritable()` waking a caller before the room it waited for exists. TinyUSB calls `tud_vendor_tx_cb` from `vendord_xfer_cb` *before* it refills the endpoint from the transmit FIFO, so at that instant the completed transfer's bytes have left the FIFO but whatever the sketch queued behind them has not - and on a dual-core part the waiter really does run its check before the usbd task reaches its own refill a few instructions later. Every wait then burned a full timeout slice: measured 1.85 MB/s against the spin loop's 21.12 on the same stream. `handleTxComplete()` now drains the FIFO before giving the semaphore, and the wait slice is 2 ms rather than 10 since a slice is also what a missed wake-up costs. Measured after: 20.94 MB/s with `stalls=0` and one block per transfer, against ~25,000 spins per 4 MiB.
- (JA) `EspUsbDeviceVendor::waitWritable()` が、待っていた空きができる前に呼び出し側を起こしていた不具合を修正しました。TinyUSB は `vendord_xfer_cb` で送信 FIFO から endpoint を再充填する**前に** `tud_vendor_tx_cb` を呼びます。その瞬間、完了した転送のバイトは FIFO を出ていますが、sketch がその後ろに積んだぶんはまだ残っています。しかもデュアルコアでは、usbd タスクが数命令先の再充填に到達する前に待機側が本当にその確認を走らせます。結果、毎回の待ちが timeout の 1 刻みを丸ごと消費し、同じストリームで spin の 21.12 MB/s に対して 1.85 MB/s しか出ていませんでした。`handleTxComplete()` で semaphore を give する前に FIFO を吸い出すようにし、待ちの刻みも 10 ms から 2 ms にしました（刻みは「空振りした起床の代償」でもあるため）。修正後の実測は 20.94 MB/s、`stalls=0`、1 転送につき 1 回の block です（従来は 4 MiB あたり約 25,000 回の spin）。
- (EN) Every class buffer size in `internal/EspUsbTinyUsbConfig.h` is now behind `#ifndef`, so a sketch raises one from its own `build_opt.h` (`-DCFG_TUD_VENDOR_TX_BUFSIZE=16384`) without copying the library - the flag reaches the library's translation units because Arduino puts `build_opt.h` on the command line for the whole build. That is the one capability owning `tusb_config.h` was supposed to buy, and leaving the sizes unguarded threw it away: the core's precompiled TinyUSB bakes them into a shipped `sdkconfig` where no sketch can reach them. Two defaults move on ESP32-P4. `CFG_TUD_VENDOR_TX_BUFSIZE` goes 512 to 4096, because 512 is one high-speed bulk packet and a sketch streaming 4 MiB out of it spins on `write()` returning 0 about 40,000 times (see the transfer-size entry above for the measurements and why 4096 rather than more). `CFG_TUD_HID_EP_BUFSIZE` goes 64 to 512, which is what the core's own P4 build has always used (`CONFIG_TINYUSB_HID_BUFSIZE=512`). Together they cost 7168 bytes of RAM on P4 whether or not a sketch uses those classes. S2/S3 keep 512/64, where a full-speed bus drains less than that anyway. Sizes above 32768 are now a build error: `tu_edpt_stream_init()` takes the FIFO size as `uint16_t` and `tu_fifo` runs its indices over `[0, 2*depth)`, so 65536 arrived as a depth of 0 and produced a device that reported ready and never mounted, with nothing pointing at the flag that caused it.
- (JA) `internal/EspUsbTinyUsbConfig.h` の class buffer サイズをすべて `#ifndef` で囲いました。sketch 側の `build_opt.h` から `-DCFG_TUD_VENDOR_TX_BUFSIZE=16384` のように上書きでき、ライブラリをコピーする必要はありません（Arduino は `build_opt.h` をビルド全体のコマンドラインに載せるので、ライブラリ自身の翻訳単位にも届きます）。これは `tusb_config.h` を自前で持っていることの唯一の対価で、囲っていなかったせいで捨てていました——core の precompiled TinyUSB では同梱 `sdkconfig` に焼かれていて sketch からは届きません。ESP32-P4 では既定値を 2 つ動かします。`CFG_TUD_VENDOR_TX_BUFSIZE` は 512 から 4096 へ。512 は high-speed bulk 1 packet 分でしかなく、4 MiB を流す sketch は `write()` が 0 を返すのを約 4 万回 spin します（実測値と、なぜ 4096 でそれ以上ではないのかは上の転送長の項目にあります）。`CFG_TUD_HID_EP_BUFSIZE` は 64 から 512 へ——core 側の P4 ビルドは元から 512 でした（`CONFIG_TINYUSB_HID_BUFSIZE=512`）。2 つ合わせて P4 で RAM 7,168 byte を消費します。そのクラスを使わない sketch も払います。S2/S3 は 512/64 のままです（full speed のバスが先に尽きるため）。32768 を超える指定はビルドエラーにしました。`tu_edpt_stream_init()` が FIFO サイズを `uint16_t` で受け、`tu_fifo` が index を `[0, 2*depth)` で回すため、65536 は depth 0 として届き、「ready だが mount しない」デバイスになっていました。原因のフラグを指すものは何も出ませんでした。
- (EN) `EspUsbDeviceHidVendor` is no longer capped at 63-byte reports. The ceiling is `CFG_TUD_HID_EP_BUFSIZE - 1` - 511 on ESP32-P4 - reported by the new `EspUsbDeviceHidVendor::maxReportSize()`, and the high-speed configuration descriptor carries a 512-byte interrupt endpoint while the full-speed one keeps the 64 USB 2.0 allows. The HID report descriptor is now built per instance so that Report Count declares the size the endpoint actually carries; it was a shared constant frozen at 63, and a host sizes its reads from that number, so raising only the endpoint would have made the host read 64 bytes of a 512-byte report. Endpoints are rewritten per HID function rather than per descriptor type, so a keyboard beside a vendor HID function keeps its 8-byte endpoint - an interrupt endpoint reserves its bandwidth, and inflating one that sends 8 bytes takes the reservation from everything else on the bus. HID is the only class that needs no driver on any host OS, and at high speed a 511-byte report at 8,000 reports/s is about 4 MB/s rather than 0.5.
- (JA) `EspUsbDeviceHidVendor` の 63 byte 上限をやめました。上限は `CFG_TUD_HID_EP_BUFSIZE - 1`（ESP32-P4 で 511）で、新しい `EspUsbDeviceHidVendor::maxReportSize()` が返します。high-speed 側の configuration descriptor は 512 byte の interrupt endpoint を持ち、full-speed 側は USB 2.0 の上限である 64 のままです。HID report descriptor は instance ごとに組み立てるようにし、Report Count が endpoint の実サイズに追従します。従来は 63 を焼き込んだ共有定数でした。ホストは読み出しサイズをこの値から決めるので、endpoint だけ上げても 512 byte の report を 64 byte しか読みません。書き換えは descriptor 種別ではなく HID function 単位なので、vendor HID の隣のキーボードは 8 byte のままです——interrupt endpoint は帯域を予約するため、8 byte しか送らないものを膨らませるとバス上の他から予約を奪います。HID はどのホスト OS でも driver が要らない唯一のクラスで、high speed では 511 byte × 8,000 report/s ≒ 4 MB/s になります（従来は 0.5 MB/s）。
- (EN) The Microsoft OS 2.0 descriptor set is no longer always wrapped in configuration and function subsets. Windows resolves a function subset through usbccgp.sys, which it loads for composite devices only; on a single-interface vendor device the subsets leave the compatible ID attached to nothing, `USB\MS_COMP_WINUSB` never reaches the device node, and Device Manager reports `CM_PROB_FAILED_INSTALL` (code 28) with not a line written to `setupapi.dev.log` - the install never starts, so there is nothing to log. The device answers the vendor request correctly the whole time, which is why it looks fine from Linux, where no driver is needed at all. The layout now follows `bNumInterfaces` by default (162 bytes flat, 178 with subsets), `EspUsbDeviceConfig::msOs20Layout` forces either shape, and `EspUsbDevice::microsoftOs20UsesSubsets()` reports what was built.
- (JA) Microsoft OS 2.0 の descriptor set を、常に configuration subset / function subset で包むのをやめました。Windows が function subset を解決するのは usbccgp.sys を通したときだけで、それを読み込むのは composite device のときだけです。単一 interface の vendor device では subset に包まれた compatible ID が結び付く先を持たず、`USB\MS_COMP_WINUSB` が device node に届かず、デバイスマネージャーは `CM_PROB_FAILED_INSTALL`（コード 28）を出します。しかも `setupapi.dev.log` には 1 行も書かれません——インストールが始まらないので記録すべきものが無いためです。その間デバイスは vendor request に正しく答え続けているので、driver の要らない Linux 側からは問題なく見えます。既定では `bNumInterfaces` に従って構造を決め（1 本なら flat の 162 byte、2 本以上なら subsets の 178 byte）、`EspUsbDeviceConfig::msOs20Layout` で強制でき、`EspUsbDevice::microsoftOs20UsesSubsets()` が結果を返します。
- (EN) New `EspUsbDevice::onAnyControlRequest()`: a pure observer that sees control requests including the ones the library answers itself. `EspUsbDeviceVendor::onControlRequest()` only ever saw the leftovers, because the library takes the WebUSB and Microsoft OS 2.0 vendor codes and the whole standard descriptor path first - which is exactly the traffic you need when a host refuses to bind a driver and you cannot tell whether it asked at all. It fires at SETUP for every vendor request, so one the device stalls still shows up, and at ACK for every control transfer that completes, carrying the byte count the library supplied. The vendored TinyUSB is untouched: the ACK side uses `dcd_edpt0_status_complete`, a weak hook `device/usbd.c` leaves for a DCD that the dwc2 driver does not define. The one thing it cannot see is a standard request the stack stalls, which has no status stage to report.
- (JA) `EspUsbDevice::onAnyControlRequest()` を追加しました。ライブラリ自身が処理した control request も含めて観測できる、戻り値で挙動を変えない callback です。`EspUsbDeviceVendor::onControlRequest()` に来るのは「残り」だけでした——WebUSB と Microsoft OS 2.0 の vendor code、そして標準 descriptor の経路はライブラリが先に処理するためで、ホストが driver を当ててくれないときに見たいのはまさにそこです。vendor request は SETUP で報告するので **STALL した要求も見え**、完了した control transfer は ACK で、ライブラリが返したバイト数付きで報告します。同梱 TinyUSB には手を入れていません。ACK 側は `device/usbd.c` が DCD 用に weak で置いていて dwc2 driver が定義していない `dcd_edpt0_status_complete` を使っています。唯一見えないのは、スタックが STALL した標準要求です（status stage が無いため）。
- (EN) `EspUsbDeviceVendor::configurationDescriptor()` was discarding the per-speed `endpointSize` it is handed and writing the constructor's value into both configurations, so a device built with 512 advertised `wMaxPacketSize=512` on a full-speed bulk endpoint - a value USB 2.0 does not define, and one a host could see through `OTHER_SPEED_CONFIGURATION`. The per-speed value is now a ceiling: a vendor built with 32 still gets 32 at full speed.
- (JA) `EspUsbDeviceVendor::configurationDescriptor()` が、渡された per-speed の `endpointSize` を捨てて constructor の値を両方の速度に書いていました。512 で作ったデバイスは full-speed の bulk endpoint にも `wMaxPacketSize=512` を出していたことになります。USB 2.0 に存在しない値で、`OTHER_SPEED_CONFIGURATION` 経由でホストから見えていました。per-speed 値は上限として使うようにしたので、32 で作った vendor は full speed でも 32 のままです。
- (EN) `EspUsbDeviceVendor` gains `writeAvailable()`, `writeCapacity()` and `waitWritable(bytes, timeoutMs)`. Until now a sketch sending more than the transmit FIFO holds could only spin on `write()` returning 0 - about 30,000 times per 4 MiB on ESP32-P4 - which is free while the CPU is idle and expensive when the same CPU is producing the data. `waitWritable()` blocks the calling task on a semaphore given from `tud_vendor_tx_cb` instead. The semaphore is created on first use, so a sketch that keeps spinning pays nothing for it.
- (JA) `EspUsbDeviceVendor` に `writeAvailable()` / `writeCapacity()` / `waitWritable(bytes, timeoutMs)` を追加しました。従来、送信 FIFO より多く送りたい sketch にできることは `write()` が 0 を返すのを spin することだけで、ESP32-P4 では 4 MiB あたり約 3 万回になります。CPU が空いていれば無害ですが、同じ CPU がデータを作っているときは高くつきます。`waitWritable()` は `tud_vendor_tx_cb` で give される semaphore で呼び出しタスクを止めます。semaphore は最初の呼び出しで作るので、spin のままの sketch は何も払いません。
- (EN) New `docs/CHANGE_REQUESTS.ja.md` answers the CR-1 to CR-9 request set raised from the ESP32-P4 high-speed measurements, including the two items not implemented: why the run-to-run spread differs from the core's stack (the core runs DWC2 in slave mode, this library in DMA mode since 2.1.0, which moves the per-transfer turnaround from an ISR onto the usbd task), and why a device does not need two transfers in flight after all (one longer transfer buys the same thing, and the measured device-side ceiling leaves single digits for anything more). It also records what the measurements settled on the bench the requests came from: Windows binding WinUSB to a single-interface vendor device, and refusing to with the old descriptor shape; 4.03 MB/s of 511-byte HID reports; and the host-side URB depth that turns out to gate both.
- (JA) `docs/CHANGE_REQUESTS.ja.md` を追加し、ESP32-P4 の high-speed 実測から起票された CR-1〜CR-9 に回答しました。実装しなかった 2 件も含みます——実行ごとのばらつきが core 内蔵 stack と違う理由（core は DWC2 を slave mode で、このライブラリは 2.1.0 以降 DMA mode で動かしており、転送ごとの折り返しが ISR から usbd タスクへ移る）と、device 側に「転送 2 本 in-flight」は結局要らないという結論（転送を 1 本長くすれば同じものが手に入り、実測した device 側の天井からすると残りは数 % しかない）。あわせて、依頼元の実験台で測って分かったこと——単一 interface の vendor device に Windows が WinUSB を bind すること、旧構造では bind しないこと、511 byte の HID report で 4.03 MB/s 出ること、その両方をホスト側の URB 深さが律速していること——も記録しています。

## 2.2.0
- (EN) `EspUsbDeviceCdcSerial` can now be registered more than once, so a device can present several USB serial ports. What bounds the count is the controller's non-control IN endpoint budget, not RAM or a class limit: each ACM function costs two IN endpoints (notification + data), so an S2/S3 fits two ports and nothing else beside them, the P4 full-speed controller the same, and the P4 high-speed controller three ports alone or two alongside HID and Vendor. `CFG_TUD_CDC` is therefore fixed per target at exactly that ceiling - 2 on S2/S3, 3 on P4 - since compiling a port that could never be enumerated buys nothing; define it before the library is built to override. Registering more than fits still fails in `begin()` with `ESP_ERR_INVALID_SIZE` before the PHY is started. Each port carries an optional name (`EspUsbDeviceCdcSerial(device, "Console")`) published as the function's `iFunction` and `iInterface` string, `port()` reports the index (which is also the TinyUSB instance the object drives, taken from descriptor order), and `EspUsbDevice::maxCdcPorts()` reports the compiled capacity. The capacity is statically allocated, so a single-port sketch also pays for it: measured +1416 bytes of RAM on S3 and +4600 bytes on P4, where the high-speed bulk endpoint buffers are 512 bytes. New example `SerialMulti`, new tests `tests/unit/cdc_multi` and `tests/peer/usb_serial_multi` (two ports on real S3 hardware: four interfaces, six endpoints, host-to-port-0 and port-0-to-host traffic, and bytes written to port 1 staying off port 0's pipe), and new P4 cases in `tests/unit/p4_controller_endpoints`.
- (EN) The device descriptor now declares `bDeviceClass/SubClass/Protocol = 0xEF/0x02/0x01` whenever the configuration it just built contains an Interface Association Descriptor, which the IAD ECN requires and which the library previously did not do - `docs/usb-device-advanced.md` had called it out as a thing worth suspecting when host driver binding misbehaves. It is what makes Windows load usbccgp.sys as the parent and bind a driver per function instead of one driver across every interface, and a device with two CDC functions needs it to show two COM ports. Configurations with no association (HID, MSC, MIDI, bulk Vendor) keep 0x00. Devices that already emit an IAD - anything with CDC, CDC-NCM or Audio - change their device descriptor as a result, so a host that cached a driver binding for that VID/PID may need the stale entry cleared; changing the PID during development is the simplest way to avoid it. New section 3.8 in the advanced guide covers the Windows side.
- (EN) `examples/SerialMulti` registers a third port on the ESP32-P4, where the high-speed controller's endpoint budget has room for it, and a new `tests/loopback/usb_serial_multi` enumerates that three-port device against a real host on one P4. That last one is worth having because the descriptor unit tests stop at "the bytes are well formed": three ports means six non-control IN endpoints, each needing its own TxFIFO carved out of the controller's 1024-word data FIFO, and dcd_dwc2 only performs that allocation at SET_CONFIGURATION. Measured on hardware: 6 interfaces, 9 endpoints (3 interrupt IN + 6 bulk), no duplicate address, `bDeviceClass=0xEF`, traffic both ways on port 0, and nothing written to ports 1 or 2 surfacing on port 0's stream. New manual test `tests/manual/cdc_multi_ports` drives every port from a PC, which is the only host here that binds a driver per function - EspUsbHost binds one CDC function per device, so the peer and loopback rigs can only drive port 0.
- (JA) `examples/SerialMulti` は ESP32-P4 で 3 本目のポートを登録します（high-speed controller の endpoint に余裕があるため）。あわせて `tests/loopback/usb_serial_multi` を追加し、P4 1 台でその 3 ポート device を実際のホストに列挙させます。これが必要なのは、descriptor の unit test が「バイト列が妥当か」までしか見ないからです——3 ポートは非 control IN endpoint を 6 本使い、IN 1 本ごとに controller の 1024 word の data FIFO から専用 TxFIFO を切り出す必要があり、その割り当ては dcd_dwc2 が SET_CONFIGURATION 時に初めて行います。実機実測: interface 6、endpoint 9（interrupt IN 3 + bulk 6）、アドレス重複なし、`bDeviceClass=0xEF`、port 0 の双方向通信、port 1 / 2 へ書いたバイトが port 0 の経路に出てこないこと。手動テスト `tests/manual/cdc_multi_ports` を追加し、PC から全ポートを 1 本ずつ叩けるようにしました——機能ごとにドライバをバインドするホストはここでは PC だけで、EspUsbHost は 1 デバイスにつき CDC 機能を 1 つしか bind しないため peer / loopback リグでは port 0 しか駆動できません。
- (EN) `tests/peer/usb_serial_multi` now verifies each port individually against EspUsbHost's per-port CDC binding, which shipped in 2.8.0: one `EspUsbHostCdcSerial` per port, traffic both ways on each, `getSerialPortInfo()` confirming the two sides agree on which function is port 0 (interfaces 0/1 with bulk 0x82/0x02) and which is port 1 (interfaces 2/3 with bulk 0x84/0x04), and `SET_LINE_CODING` on port 1 leaving port 0's baud alone - the control path being per-port, not just the data path. Requires EspUsbHost 2.8.0.
- (JA) `tests/peer/usb_serial_multi` を、EspUsbHost の per-port CDC bind（2.8.0 の機能）に対して**ポートごとに個別検証**する形へ更新しました。`EspUsbHostCdcSerial` をポートごとに 1 つ bind し、各ポートで双方向通信を確認、`getSerialPortInfo()` で「どちらの機能を port 0 と呼ぶか」が両者で一致していること（port 0 = interface 0/1・bulk 0x82/0x02、port 1 = interface 2/3・bulk 0x84/0x04）、そして port 1 への `SET_LINE_CODING` が port 0 の baud を動かさないこと——データ経路だけでなく制御経路も per-port であること——まで確認します。EspUsbHost 2.8.0 が必要です。
- (EN) `tests/loopback/usb_serial_multi` now drives all three of the P4 device's CDC ports individually - traffic both ways on each, `getSerialPortInfo()` mapping every port onto the interfaces and endpoints the device emitted, separation, and per-port `SET_LINE_CODING`. Three ports fit the host controller only because EspUsbHost stopped claiming the CDC control interface and sends its class requests over EP0: a port costs two channels (bulk IN + OUT) rather than three, so EP0 plus 3 x 2 is 7 of the full-speed controller's 8. No build configuration is involved. Requires EspUsbHost 2.8.0.
- (JA) `tests/loopback/usb_serial_multi` が P4 device の CDC 3 ポートすべてを個別に駆動するようになりました——各ポートの双方向通信、`getSerialPortInfo()` による全ポートの interface / endpoint 対応、分離、per-port の `SET_LINE_CODING`。3 ポートが host コントローラに収まるのは、EspUsbHost が CDC control interface を claim せず class request を EP0 で送るようになったからで、1 ポートが 3 チャネルではなく 2 チャネル（bulk IN + OUT）で済みます——EP0 1 本 + 3 × 2 = full-speed コントローラの 8 チャネル中 7 本。ビルド設定は一切要りません。EspUsbHost 2.8.0 の per-port bind を使います。
- (EN) Peer and loopback tests pin EspUsbHost 2.8.0, the release carrying per-port CDC binding.
- (JA) peer / loopback テストの EspUsbHost ピンを 2.8.0 に上げました。per-port CDC bind を含むリリースです。
- (EN) `MAX_CLASSES` raised from 4 to 6. The P4 high-speed controller's 7 non-control IN endpoints admit HID + MSC + Vendor + two CDC ports, which is five functions, so the old registration ceiling would have refused a device the endpoint budget allows. Each slot is one pointer.
- (JA) `EspUsbDeviceCdcSerial` を複数登録できるようにしました。1 台のデバイスが USB シリアルポートを複数持てます。本数を決めるのは RAM でもクラス数でもなく、コントローラの非 control IN endpoint です——ACM 1 機能につき IN 2 本（通知＋データ）なので、S2/S3 は 2 ポートでちょうど使い切り（他は何も入りません）、P4 の full-speed controller も同じ、P4 の high-speed controller は単独で 3 ポート、HID と Vendor と併用なら 2 ポートです。したがって `CFG_TUD_CDC` はターゲットごとにその上限へ決め打ちしました（S2/S3 は 2、P4 は 3）。列挙できないポートをコンパイルしても意味がないからです。上書きしたい場合はライブラリのビルド前に定義してください。上限を超えて登録した場合は従来どおり `begin()` が PHY 起動前に `ESP_ERR_INVALID_SIZE` で失敗します。各ポートには名前を付けられ（`EspUsbDeviceCdcSerial(device, "Console")`）、IAD の `iFunction` と control インターフェースの `iInterface` として公開されます。`port()` はポート番号（＝そのオブジェクトが駆動する TinyUSB インスタンス番号。ディスクリプタ順に決まります）を、`EspUsbDevice::maxCdcPorts()` はビルド時の容量を返します。容量は静的に確保されるので、1 ポートしか使わないスケッチにもコストがかかります——実測で S3 が RAM +1416 byte、P4 が +4600 byte（P4 は high-speed の bulk endpoint buffer が 512 byte のため）。example `SerialMulti`、テスト `tests/unit/cdc_multi` と `tests/peer/usb_serial_multi`（S3 実機 2 台で 2 ポート: インターフェース 4・エンドポイント 6、host→port0 と port0→host の通信、port 1 へ書いたバイトが port 0 の経路に出てこないこと）、および `tests/unit/p4_controller_endpoints` への P4 ケース追加を伴います。
- (JA) 組み上げたコンフィグレーションディスクリプタに Interface Association Descriptor が含まれるとき、device descriptor の `bDeviceClass/SubClass/Protocol` に `0xEF/0x02/0x01` を宣言するようにしました。IAD ECN が要求している内容ですが、これまでこのライブラリはやっておらず、`docs/usb-device-advanced.ja.md` にも「ホスト側のドライバのバインドがおかしいときに疑う価値がある」と書いてあった箇所です。Windows で usbccgp.sys が親としてロードされ、全インターフェースを 1 つのドライバが掴むのではなく機能ごとにドライバがバインドされる条件がこれで、CDC を 2 つ持つデバイスが COM ポートを 2 つ見せるにはこの宣言が要ります。IAD を出さない構成（HID・MSC・MIDI・bulk Vendor）は 0x00 のままです。既に IAD を出していたデバイス——CDC・CDC-NCM・Audio を含むもの——は device descriptor が変わるので、その VID/PID でドライバのバインドをキャッシュ済みのホストでは古いエントリの削除が必要になることがあります。開発中は PID を変えるのが一番簡単な回避策です。Windows 側の扱いは上級編に 3.8 節として追加しました。
- (JA) `MAX_CLASSES` を 4 から 6 に引き上げました。P4 の high-speed controller は非 control IN endpoint が 7 本あり、HID + MSC + Vendor + CDC 2 ポート（5 機能）が収まります。従来の登録上限では、エンドポイント的には成立する構成を拒否してしまっていました。1 スロットはポインタ 1 個です。

## 2.1.0
- (EN) Fix a permanent stop of the CDC-NCM data path under sustained device-to-host traffic, reported from the field as "the usb network drops requiring esp32 restart" while streaming WebSocket data out of the ESP32. The report suggested raising the USB task priority; that was a dead end, because `startTinyUsbRuntime()` already creates the task at `configMAX_PRIORITIES - 1` (24 on Arduino-ESP32) and `EspUsbDeviceConfig` never exposed the knob, so there was nothing to raise. A new `tests/peer/usb_ncm_soak` rig reproduced it on the first run and in 1-4 seconds, not "after a while": device-to-host throughput went to zero and never returned, while host-to-device kept working and no panic, assert or watchdog was ever logged. Dumping the dwc2 registers at the stall showed a bulk IN endpoint in a state the hardware cannot leave - `EPENA=1` with packets still outstanding, `XFRSIZ=0` (the driver had written every byte into the TxFIFO), an empty TxFIFO, and `DIEPEMPMSK` already cleared. `handle_epin_slave()` in TinyUSB's dwc2 driver disarms the FIFO-empty interrupt as soon as `XFRSIZ` reaches zero, and that interrupt is the driver's only way to refill the FIFO; if the FIFO's contents are then lost before the packets go out, nothing can ever feed the transfer again. The transfer-complete interrupt therefore never arrives, NCM never gets its NTB back, and `tud_network_can_xmit()` stays false forever - the USB network is dead until reboot. The fix is to run the dwc2 device driver in DMA mode, which does not use the manual FIFO refill path at all. Measured on the two-board S3 rig: device-to-host now sustains 5.95 Mbps for 300 seconds and 223 MB with zero stalled seconds, a 4 ms worst-case gap and flat heap, against permanent death after roughly 2 MB before. That is also about 1.9x the 3.20 Mbps this direction managed before it died.
- (JA) device → host 方向の連続通信で CDC-NCM のデータ経路が恒久停止する不具合を修正しました。ESP32 から WebSocket でデータを流し続けると「USB ネットワークが落ち、ESP32 の再起動が必要になる」という報告によるものです。報告では USB タスクの優先度を上げたいという要望が添えられていましたが、これは打つ手がありません——`startTinyUsbRuntime()` は既に `configMAX_PRIORITIES - 1`（Arduino-ESP32 では 24）でタスクを生成しており、`EspUsbDeviceConfig` にその設定を露出してもいないため、上げる余地がゼロだからです。新設した `tests/peer/usb_ncm_soak` で 1 回目の実行から再現し、しかも「しばらく後」ではなく **1〜4 秒**で停止しました。device → host のスループットがゼロに張り付いたまま戻らない一方、host → device 方向は生き続け、panic・assert・watchdog の類はログに一切現れません。停止時に dwc2 のレジスタをダンプしたところ、bulk IN エンドポイントがハードウェア的に脱出不能な状態にありました——`EPENA=1` で未送信パケットが残り、`XFRSIZ=0`（ドライバは全バイトを TxFIFO へ書き終えている）、しかし TxFIFO は空、そして `DIEPEMPMSK` は解除済み。TinyUSB の dwc2 ドライバの `handle_epin_slave()` は `XFRSIZ` がゼロになった時点で FIFO empty 割り込みを解除しますが、これはドライバがその FIFO を補充する唯一の手段です。その後にパケットが送出されないまま FIFO の内容が失われると、二度と転送に給餌できません。結果として転送完了割り込みが永久に来ず、NCM は NTB を回収できず、`tud_network_can_xmit()` が永久に false になります——再起動するまで USB ネットワークは死んだままです。修正は dwc2 デバイスドライバを DMA モードで動かすことです。この経路は手動 FIFO 補充を一切使いません。S3 の 2 台構成での実測値は、device → host が 300 秒・223 MB を **5.95 Mbps** で停止秒数ゼロ、最大無通信 4 ms、ヒープは平坦。従来はおよそ 2 MB 流したところで恒久的に死んでいました。死ぬ前に出ていた 3.20 Mbps と比べても約 1.9 倍です。
- (EN) DMA mode is selected per target, and the two DWC2 transfer modes must never both be enabled. `CFG_TUD_EDPT_DEDICATED_HWFIFO` is derived from `CFG_TUD_DWC2_SLAVE_ENABLE`, and that flag decides at compile time whether the shared `tu_edpt_stream` layer hands the driver a real endpoint buffer or a `tu_fifo`. Enabling both - in the hope that `dma_device_enabled()` would pick the working one from the controller's `GHWCFG2` at run time - leaves CDC, MIDI and Vendor calling `usbd_edpt_xfer_fifo()`, whose `xfer->buffer` is NULL, so the endpoint DMAs from address 0 and the host receives whatever lives there; 34 tests failed that way, in exactly the classes built on that layer, while HID, MSC and NCM passed because they pass real buffers. There is therefore no run-time fallback for a controller without internal DMA, which makes the choice worth checking rather than guessing: ESP-IDF records each core's configuration in `soc/usb_dwc_cfg.h`, and S2 and S3 both carry `OTG_ARCHITECTURE 2` - `GHWCFG2_ARCH_INTERNAL_DMA`, exactly what `dma_device_enabled()` requires. All three targets therefore use DMA. S3 and P4 are measured on hardware; S2 rests on that constant and a compile check, since no S2 board is available here.
- (JA) DMA モードはターゲットごとに選択し、DWC2 の 2 つの転送モードは決して同時に有効にしてはいけません。`CFG_TUD_EDPT_DEDICATED_HWFIFO` は `CFG_TUD_DWC2_SLAVE_ENABLE` から導出され、このフラグが「共通の `tu_edpt_stream` 層がドライバに実バッファを渡すか `tu_fifo` を渡すか」を**コンパイル時に**決めます。`dma_device_enabled()` が実行時に `GHWCFG2` を見て動く方を選んでくれることを期待して両方を有効にすると、CDC・MIDI・Vendor は `usbd_edpt_xfer_fifo()` を呼び、その `xfer->buffer` は NULL なので、エンドポイントはアドレス 0 から DMA 読み出しを行い、Host にはそこにある内容がそのまま届きます。この状態で 34 件のテストが失敗し、失敗したのはまさにこの層の上に構築されたクラスだけでした（HID・MSC・NCM は実バッファを渡すため成功）。したがって内部 DMA を持たないコントローラに対する実行時フォールバックは存在せず、この選択は推測ではなく確認すべきものになります。ESP-IDF は各コアの構成を `soc/usb_dwc_cfg.h` に記録しており、S2 と S3 はいずれも `OTG_ARCHITECTURE 2`——`dma_device_enabled()` が要求する `GHWCFG2_ARCH_INTERNAL_DMA` そのものです。よって 3 ターゲットとも DMA を使用します。S3 と P4 は実機で計測済みで、S2 はこの定数とビルド確認のみが根拠です（S2 実機が手元にないため）。
- (EN) Hand every `tud_network_*` call to the usbd task. `tud_network_xmit()` does not merely record the frame: it copies it through `tud_network_xmit_cb()` and goes all the way to `usbd_edpt_xfer()` in the caller's context, and TinyUSB requires its device API to be called from the same context as `tud_task()` (`device/usbd.h`). The esp_netif transmit hook was calling it from lwIP's tcpip task, so the two ran concurrently - instrumenting both sides measured 2211 of 3091 transfer completions (72%) landing inside such a call under load. This was not the cause of the stall above, which reproduces with the race removed, but it is a documented contract violation on the endpoint state and worth closing on its own. Producers now copy the frame into a pooled buffer and queue it; `espUsbDeviceNetDrainTx()`, called from the `tud_task()` loop, is the only place in the library that touches the network API. This also deletes the old wait structure, which polled `tud_network_can_xmit()` for up to 100 ticks and then waited another 100 ms for a completion that (because the copy is synchronous) had already happened - up to 200 ms per frame on a busy link. A full queue now drops the frame and lets TCP retransmit, rather than blocking the tcpip task and every other interface with it. The stale comment claiming `tud_network_xmit` only records the frame is gone with it.
- (JA) `tud_network_*` の呼び出しをすべて usbd タスクへ寄せました。`tud_network_xmit()` はフレームを記録するだけの関数ではなく、`tud_network_xmit_cb()` でコピーし呼び出し元のコンテキストのまま `usbd_edpt_xfer()` まで到達します。そして TinyUSB は device API を `tud_task()` と同じコンテキストから呼ぶことを要求しています（`device/usbd.h`）。esp_netif の transmit フックはこれを lwIP の tcpip タスクから呼んでいたため両者が並行実行されており、双方に計測を入れたところ負荷時に転送完了 3091 回のうち **2211 回（72%）**がその呼び出しの内側で発生していました。上記の停止の原因はこれではなく（競合を除去しても再現します）、しかしエンドポイント状態に対する明文の規約違反であり、それ自体を塞ぐ価値があります。送信側はフレームをプール済みバッファへコピーしてキューに積むだけになり、`tud_task()` ループから呼ばれる `espUsbDeviceNetDrainTx()` が、ライブラリ内でネットワーク API に触れる唯一の場所になりました。これに伴い旧来の待ち構造も削除されます——`tud_network_can_xmit()` を最大 100 tick ポーリングし、さらに（コピーが同期実行である以上すでに完了している）完了を 100ms 待つもので、混雑時には 1 フレームあたり最大 200ms を要していました。キューが満杯のときはフレームを落として TCP の再送に任せます。tcpip タスクを止めて他の全インターフェースを巻き込むよりも健全です。`tud_network_xmit` はフレームを記録するだけ、と述べていた実装と乖離したコメントも同時に消えます。
- (EN) Raise the NCM NTB pool sizes to TinyUSB's own recommended values: `CFG_TUD_NCM_IN_NTB_N` 1 to 3 and `CFG_TUD_NCM_OUT_NTB_N` 1 to 2. Upstream measures up to 50% more throughput at 2 and never sees `tud_network_can_xmit: request blocked` at 3 (`class/net/ncm.h`). At the default of 1 there is a single transmit NTB, so every frame waits for the previous transfer to complete, and a single lost NTB takes the whole transmitter with it. This costs about 9.6 KB of USB-capable RAM and is not a fix for the stall on its own.
- (JA) NCM の NTB プール数を TinyUSB 自身の推奨値へ引き上げました（`CFG_TUD_NCM_IN_NTB_N` を 1 から 3、`CFG_TUD_NCM_OUT_NTB_N` を 1 から 2）。上流の計測では 2 で最大 50% のスループット向上、3 では `tud_network_can_xmit: request blocked` が発生しなくなるとされています（`class/net/ncm.h`）。既定の 1 では送信 NTB が 1 枚しかないため、全フレームが直前の転送完了を待つ上、その 1 枚を失うと送信全体が道連れになります。USB 用 RAM を約 9.6 KB 消費します。単体では上記の停止の修正にはなりません。
- (EN) New `tests/peer/usb_ncm_soak`: a two-board CDC-NCM soak that streams device-to-host for 30 and 300 seconds and fails on the reported symptom rather than on averages. It asserts that no second of the stream had zero bytes, that the worst gap stayed under 5 seconds, that `tud_network_can_xmit()` is still true afterwards, and that a fresh TCP connection still carries data - a link that recovers on its own is not the reported failure, and a link that dies in the last second would still pass a throughput-only check.
- (JA) `tests/peer/usb_ncm_soak` を追加しました。2 台構成で device → host を 30 秒／300 秒連続で流す soak テストで、平均値ではなく報告された症状そのもので失敗します。ストリームのどの 1 秒もゼロバイトでなかったこと、最悪の無通信が 5 秒未満だったこと、終了後も `tud_network_can_xmit()` が true であること、新規 TCP 接続が依然としてデータを運べることを検証します——自力で回復するリンクは報告された故障ではなく、また最後の 1 秒で死んだリンクはスループットだけの検査では通ってしまうからです。
- (EN) New `tests/loopback/usb_ncm`, the first NCM coverage on P4 at all: EspUsbDevice drives the NCM function on one port of a single board and EspUsbHost consumes it on the other. Both sides use raw frames (`sendFrame()` / `networkReadFrame()`) rather than lwIP, because two netifs on one chip share a stack, so TCP between them would be routed internally and never cross the USB bus - the test would pass without touching the code under test. P4 measured 8.76 Mbps with zero stalled seconds in DMA mode and 8.74 Mbps in slave mode, i.e. it does not reproduce the stall either way here. That is not proof P4 is unaffected: the loopback rig has the host on the full-speed port, so the link runs at Full Speed and the P4's high-speed controller is still uncovered, and the traffic is fixed 1514-byte frames rather than the variable bursts lwIP produces.
- (JA) `tests/loopback/usb_ncm` を追加しました。P4 における NCM の初めてのカバレッジです。1 枚のボードの片方のポートで EspUsbDevice が NCM を動かし、もう片方で EspUsbHost が受けます。両側とも lwIP ではなく生フレーム（`sendFrame()` / `networkReadFrame()`）を使います。1 チップ上の 2 つの netif は同じスタックを共有するため、その間の TCP は内部で経路制御されて USB バスを一切通らず、テスト対象のコードに触れないまま成功してしまうからです。P4 の実測は DMA モードで 8.76 Mbps・停止秒数ゼロ、slave モードで 8.74 Mbps で、ここではどちらでも停止を再現しません。ただしこれは P4 が影響を受けないことの証明にはなりません——loopback は Host を Full-Speed ポートに置く構成のためリンクは Full Speed で確立し、P4 の High-Speed コントローラは依然として未カバーであり、またトラフィックも lwIP が生む可変長バーストではなく 1514 byte 固定だからです。

## 2.0.2
- (EN) `EspUsbDeviceMidi(device, cableCount)` exposes up to 16 cables on one MIDI interface, each of which the host lists as a separate MIDI port. Requested by the `EspMidi` integration library, whose model is one endpoint carrying up to 16 ports; the USB Device side was the only half that could not provide more than one. Nothing but the descriptor was in the way: the 4-byte packet API already carries the cable number in the header's high nibble, and TinyUSB's MIDI driver only moves bytes across the bulk endpoints without interpreting jacks. TinyUSB's `TUD_MIDI_DESCRIPTOR()` template is fixed at one cable, so the head, per-cable jack descriptors and endpoint blocks it is built from are now emitted directly - every one of them was already parameterized by the cable count. Interface and endpoint counts do not change, because cables multiplex one pair of bulk endpoints. The default is 1 cable and its descriptor is byte-for-byte the old one, so existing sketches are unaffected. Every message helper takes a 0-based `cable` argument defaulting to 0, and fails for a cable at or above `cableCount()` rather than emitting a packet that would land on a port the host was never told about. `MAX_CONFIG_DESCRIPTOR` grows from 256 to 704 bytes because a 16-cable MIDI function is 572 bytes on its own (three buffers of that size are held per device, so this costs 1344 bytes of RAM); `configurationDescriptorForSpeed()` is now overridden to check the caller's capacity *before* writing, since this is the first function whose descriptor can genuinely outgrow the buffer - the base implementation discards the capacity argument, so a longer descriptor would previously have overrun it. A second constructor, `EspUsbDeviceMidi(device, inCableCount, outCableCount)`, gives the two directions different cable counts as many real MIDI interfaces have; both names are host-view, like USB endpoint directions and EspUsbHost's `EspUsbHostMidiPortInfo`, so IN is device to host. TinyUSB's jack template always emits all four jacks of a cable and so can only describe a two-way cable, so a one-way cable's two jacks are emitted from templates of this library's own, and the MS header's `wTotalLength` - which the template derives from a single cable count - is overwritten with the length actually written. Send helpers are bounded by `inCableCount()`, so a cable that exists only for receiving is refused. New `inCableCount()`, `outCableCount()` and `descriptorLength()` accessors. Per-cable names (the jack descriptor's string index) are not implemented: the string descriptor table is a hardcoded chain of indices 1-3 plus a conditional index 4 for the network MAC, so naming would need a real string table first, and hosts name the ports themselves in the meantime. New host-side test `tests/unit/midi_descriptor` compiles the real descriptor builder with g++ and checks every field for all 16 symmetric counts and all 240 asymmetric combinations - the MS header's `wTotalLength`, each endpoint descriptor's `bNumEmbMIDIJack` and trailing jack ID list, jack ID uniqueness, the `bLength` walk, and that one cable still matches `TUD_MIDI_DESCRIPTOR()` byte for byte. New `tests/loopback/usb_midi_cables` covers a 4-cable device in both directions on one P4, and `tests/peer/usb_midi_cables` an asymmetric 4-in / 5-out one across two boards - asymmetric because the class names embedded jacks from the device's side, the opposite of the endpoint direction they belong to, so a host that swaps the two directions is invisible to a symmetric device and to any round trip; the measured `in=4 out=5` is the only proof the mapping is right. The peer test is the only rig that can see what the host decoded, so it asserts the cable counts `getMidiPortInfo()` reports both before any MIDI traffic and again after traffic has been seen on every cable (a count accumulated from observed packets rather than read from the descriptors fails), plus several cables inside one bulk transfer and SysEx on a non-zero cable; it needs EspUsbHost's unreleased `getMidiPortInfo()` (run with `--profile s3_peer_local`). A CS_ENDPOINT on a non-MIDI endpoint (which needs a composite Audio + MIDI device) and a device with two MIDI Streaming interfaces stay uncovered there because `EspUsbDeviceMidi` cannot produce those devices.
- (JA) `EspUsbDeviceMidi(device, cableCount)` で 1 つの MIDI interface 上に最大 16 本の cable を公開できるようにしました。Host からは cable ごとに別々の MIDI port として並びます。統合ライブラリ `EspMidi` からの要求で、同ライブラリのモデルが「1 endpoint あたり最大 16 port」であるのに対し、USB Device 側だけが 1 本しか出せませんでした。妨げになっていたのは descriptor だけです——4 byte packet API は既に header の上位 nibble で cable 番号を運んでおり、TinyUSB の MIDI driver は jack を解釈せず bulk endpoint 上のバイト列を運ぶだけです。TinyUSB の `TUD_MIDI_DESCRIPTOR()` template は cable 1 本固定なので、その構成要素（head、cable ごとの jack descriptor、endpoint block）を直接出力するようにしました。いずれも元から cable 数で parameterize されています。cable は 1 組の bulk endpoint を多重化する概念なので、interface 数と endpoint 数は変わりません。既定は 1 本で、その descriptor は従来と byte 単位で同一なので既存スケッチに影響はありません。各 message helper は 0 始まりの `cable` 引数（既定 0）を取り、`cableCount()` 以上の cable に対しては、Host が知らない port に載る packet を出す代わりに失敗します。`MAX_CONFIG_DESCRIPTOR` を 256 から 704 byte へ拡張しました（16 cable の MIDI function 単体で 572 byte。この大きさの buffer を device あたり 3 本持つため RAM 1344 byte 増）。あわせて `configurationDescriptorForSpeed()` を override し、書き込む**前に**呼び出し側の容量を検査するようにしました。descriptor が実際に buffer を超え得る最初の function であり、基底実装は容量引数を捨てるため、従来のままでは overrun していました。第 2 のコンストラクタ `EspUsbDeviceMidi(device, inCableCount, outCableCount)` で方向ごとに異なる cable 数を指定できます（実際の MIDI インターフェースでは非対称が普通です）。名前はどちらも Host から見た方向で、USB の endpoint 方向や EspUsbHost の `EspUsbHostMidiPortInfo` と同じく IN が device → Host です。TinyUSB の jack template は cable の 4 jack を必ず出力するため双方向の cable しか表現できないので、片方向 cable の 2 jack は本ライブラリ側の template で出力し、MS header の `wTotalLength`（template は単一の cable 数から算出するため非対称を表現できない）は実際に書いた長さで上書きします。送信 helper の上限は `inCableCount()` なので、受信専用の cable への送信は失敗します。`inCableCount()` / `outCableCount()` / `descriptorLength()` を追加しました。cable ごとの名前（jack descriptor の string index）は未実装です——string descriptor table が index 1-3 のハードコードと network MAC 用の条件付き index 4 だけの構造で、名前付けにはまず本物の string table が必要であり、それまでは Host 側が port 名を付けます。host 側 test `tests/unit/midi_descriptor` を追加し、実際の descriptor 生成コードを g++ でコンパイルして 対称 16 通りと非対称 240 通りすべての組み合わせについて全フィールドを検証します——MS header の `wTotalLength`、各 endpoint descriptor の `bNumEmbMIDIJack` と末尾の jack ID 列、jack ID の一意性、`bLength` による走査、そして cable 1 本のとき `TUD_MIDI_DESCRIPTOR()` と byte 単位で一致すること。`tests/loopback/usb_midi_cables`（P4 1 台、4 cable の双方向）と `tests/peer/usb_midi_cables`（2 台構成、非対称 4-in / 5-out）を追加しました。非対称にしたのは、クラス仕様が embedded jack を endpoint の方向とは逆向きに命名するため、Host が 2 方向を入れ替えていても対称な device や往復テストでは検出できないからです。実測した `in=4 out=5` がマッピングの正しさを示す唯一の証拠になります。peer test は Host が何を読み取ったかを確認できる唯一の構成なので、`getMidiPortInfo()` の cable 数を **MIDI 通信の前**と全 cable に通信を流した後の両方で検証し（観測した packet から数を積み上げる実装なら失敗します）、さらに 1 回の bulk transfer への cable 混在と 0 以外の cable での SysEx もカバーします。これには未リリースの `getMidiPortInfo()` が必要です（`--profile s3_peer_local` で実行）。MIDI 以外の endpoint に付く CS_ENDPOINT（Audio + MIDI の composite が必要）と MS インターフェースを 2 つ持つ device は、`EspUsbDeviceMidi` がその device を作れないため peer test では未カバーです。
- (EN) Document a host-side limit found while testing the above: an ESP-IDF USB host (so EspUsbHost, on any target) refuses a configuration descriptor longer than its enumeration control transfer, and `CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE` is 256 in the precompiled Arduino libraries with no way to raise it from a sketch. For a MIDI-only device that caps the cable count at **5 per direction**: measured on the two-board rig, a symmetric 5-cable device is 220 bytes of MIDI descriptor plus the 9-byte configuration header = 229 and enumerates, while 6 cables is 261 and fails with `ENUM: Configuration descriptor larger than control transfer max length` / `CHECK_SHORT_CONFIG_DESC FAILED` - the device never appears at all, so this is not a partial-enumeration case. All 16 cables are legal USB and a PC host accepts them; the limit belongs to that host stack, not to this library, and applies to any function whose descriptor is long, not only MIDI. README and TEST_PLAN record it, and `tests/peer/usb_midi_cables` uses 5 cables because of it.
- (JA) 上記のテスト中に判明した Host 側の制約を記載しました。ESP-IDF の USB Host（つまり EspUsbHost。ターゲットに関わらず）は enumeration の control transfer より長い configuration descriptor を拒否し、`CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE` は Arduino のプリコンパイル済みライブラリで 256 固定、スケッチからは変更できません。MIDI 単機能の device では cable 数の上限が方向あたり **5 本**になります。2 台構成の実機で測定した結果、対称 5 cable は MIDI descriptor 220 byte + configuration header 9 byte = 229 byte で enumerate でき、6 cable は 261 byte で `ENUM: Configuration descriptor larger than control transfer max length` / `CHECK_SHORT_CONFIG_DESC FAILED` となります。device がそもそも現れないため、部分的に enumerate されるケースではありません。16 本まで USB 仕様上は正当で PC の Host は受け付けます。制約はその Host スタック側にあり本ライブラリ側ではなく、また MIDI に限らず descriptor が長い function すべてに当てはまります。README と TEST_PLAN に記載し、`tests/peer/usb_midi_cables` はこの制約により 5 cable を使っています。
- (EN) Move the three configuration descriptor buffers off the object and onto the heap, allocated once on the first `buildDescriptors()` and released by the destructor. Raising `MAX_CONFIG_DESCRIPTOR` to 704 for multi-cable MIDI had grown `EspUsbDevice` by 1344 bytes, and `tests/unit/descriptor` puts four of them on the stack at once in its class-lifecycle case: that overflowed the Arduino loop task and the board panicked with `Stack canary watchpoint triggered (loopTask)` on every run. Sketches are expected to declare the device globally, but a stack instance has always worked and must keep working, so the fix is to stop carrying 2 KB of buffers inside the object rather than to change the test - the object is now smaller than it was before the cable work. `EspUsbDevice` is no longer copyable, which it never meaningfully was (it registers itself with global callback targets); the declaration turns what would now be a double free into a build error.
- (JA) configuration descriptor の buffer 3 本をオブジェクトからヒープへ移し、最初の `buildDescriptors()` で 1 回だけ確保してデストラクタで解放するようにしました。複数 cable MIDI のために `MAX_CONFIG_DESCRIPTOR` を 704 に拡張したことで `EspUsbDevice` が 1344 byte 大きくなり、`tests/unit/descriptor` の class lifecycle ケースが 4 個を同時にスタックへ置くため、Arduino の loop task のスタックを溢れさせて毎回 `Stack canary watchpoint triggered (loopTask)` で panic していました。スケッチは device をグローバルに置く想定ですが、スタック上のインスタンスは従来動いており今後も動くべきなので、テストを変えるのではなく 2 KB の buffer をオブジェクト内に抱えるのをやめる方向で直しました。結果としてオブジェクトは cable 対応前より小さくなっています。あわせて `EspUsbDevice` をコピー不可にしました。グローバルな callback target に自身を登録する型なのでコピーは元々意味がなく、この宣言により二重解放になるコードがビルドエラーになります。

## 2.0.1
- (EN) Do not lose a CCID slot change that happens while the previous notification is still unpolled. The interrupt IN endpoint holds one notification at a time, and the host does not poll it at all until it opens the interface, so the notification sent at mount can sit there indefinitely; any card movement in that window was dropped and the host kept a stale view until it polled the slot status over bulk. The device now remembers that the host is behind and sends the state the slot ended up in as soon as the endpoint is free.
- (JA) 直前の通知がまだ Host に取り出されていない間に起きた CCID の slot 変化を取りこぼさないようにしました。interrupt IN endpoint は同時に 1 件しか保持できず、Host は interface を open するまでそもそも polling しないため、mount 時に送る通知がそのまま残り続けることがあります。その間のカード挿抜は破棄され、Host は bulk で slot status を取りに行くまで古い状態のままでした。Host が遅れていることを覚えておき、endpoint が空き次第そのときの状態を送るようにしています。
- (EN) Add `tests/peer/usb_audio_uac2`, the first end-to-end validation of the UAC2 path against a real UAC2 host (EspUsbHost 2.7.1; until now the host side was UAC1-only, which is why UAC2 streaming was documented as unvalidated). The peer is a UAC2 headset and the assertions are deliberately on the device, unlike the same-named test in the EspUsbHost repository which asserts what the host learned: the volume and mute the host writes are read back through the device's own `getVolume()` / `getMute()` and the events it raised, for the Feature Unit's master *and* logical channel (two different entries of UAC2's 4-byte `bmaControls`); the sample rate is accepted on the Clock Source entity rather than the UAC1 endpoint request; exactly two streams enumerate, so the asynchronous playback interface's explicit feedback IN endpoint is not mistaken for a third; and both isochronous directions carry PCM while the host paces its OUT packets from the feedback rate the device reports. Rate switching is not covered: the descriptor builder emits one alternate setting per direction, so a UAC2 function declares exactly one rate and the Clock Source has nothing to switch between - the multi-subrange `RANGE` encoder stays covered by `tests/unit/audio_model`. README and TEST_PLAN no longer describe UAC2 streaming as pending.
- (JA) `tests/peer/usb_audio_uac2` を追加しました。実際のUAC2 host（EspUsbHost 2.7.1）に対するUAC2経路の初のend-to-end検証です（これまでHost側がUAC1専用だったため、UAC2 streamingは未検証と記載していました）。peerはUAC2 headsetで、EspUsbHostリポジトリの同名テスト（Hostが何を読み取れたかを検証する）とは対照的に、検証対象をdevice側に置いています——Hostが書いたvolume / muteをdevice自身の `getVolume()` / `getMute()` と発火したeventで読み戻して一致を確認し、それをFeature Unitのmasterとlogical channelの両方（UAC2の4 byte `bmaControls` の別エントリ）で行います。sample rateはUAC1のendpoint requestではなくClock Source entityで受け付けること、streamがちょうど2本＝非同期playback interfaceのexplicit feedback IN endpointが3本目と誤認されないこと、双方向にPCMが流れる間もHostがdeviceの申告するfeedback rateでOUTをpacingしていることを検証します。rate切り替えは対象外です——descriptor builderが方向ごとにalternate settingを1つだけ出力するため、UAC2 functionが宣言するrateは1つで、Clock Sourceに切り替える先が無いためです（複数subrangeの `RANGE` encoderは引き続き `tests/unit/audio_model` でカバー）。READMEとTEST_PLANからUAC2 streamingが未検証である旨の記述を削除しました。
- (EN) Add `EspUsbDeviceCcid`, a USB CCID smart card reader function, so a board can be the device half of EspUsbHost 2.7.1's `ccid*` API and of any PC/SC host. The library is the reader - one slot, `bInterfaceClass` 0x0b with bulk IN/OUT and an interrupt IN, a CCID class descriptor declaring T=1 and short APDU level exchange - and the sketch is the card: `insertCard(atr, length)` / `removeCard()` set presence and the ATR, `onApdu()` answers each exchange with the response including SW1SW2, `onEscape()` handles vendor traffic, and `onPower()` reports activation. Slot status, activation, GetParameters / SetParameters / ResetParameters, IccClock / T0APDU, and the ABORT sequence are answered by the class itself; an unsupported message type comes back as CMD_NOT_SUPPORTED rather than a stall, and a message larger than the buffer as XFR_OVERRUN. TinyUSB has no CCID driver and the copy under `src/` is vendored upstream-verbatim, so the driver is registered through TinyUSB's application class-driver hook (`usbd_app_driver_get_cb`) instead of patching it in. That hook lives in `internal/EspUsbDeviceAppDriver` and only reads a pointer the CCID class sets in its `begin()`: naming the driver directly there would have pulled it into every sketch (measured: +2396 bytes flash), whereas the indirection costs 16 bytes and leaves the driver linked only when a sketch instantiates the class. Message buffers are heap-allocated in `begin()` for the same reason. New example `SmartCardReader`, new peer test `tests/peer/usb_ccid`, and a host-side descriptor test `tests/unit/ccid_descriptor` that compiles the real descriptor builder with g++ and checks every class-descriptor field.
- (JA) USB CCID スマートカードリーダー機能 `EspUsbDeviceCcid` を追加しました。EspUsbHost 2.7.1 の `ccid*` API や PC/SC ホストの相手側をボードで務められます。リーダー側がライブラリ（1 slot、`bInterfaceClass` 0x0b の bulk IN/OUT + interrupt IN、T=1 と short APDU level exchange を宣言する CCID class descriptor）、カード側がスケッチです——`insertCard(atr, length)` / `removeCard()` でカードの有無と ATR を決め、`onApdu()` が SW1SW2 込みの応答を返し、`onEscape()` が vendor 固有のやり取りを、`onPower()` が活性化を扱います。slot status、活性化、GetParameters / SetParameters / ResetParameters、IccClock / T0APDU、ABORT 手順はクラス側で応答します。未対応の message type は STALL ではなく CMD_NOT_SUPPORTED、buffer を超える message は XFR_OVERRUN として返します。TinyUSB に CCID driver は無く、`src/` の同梱コピーは upstream そのままなので、driver は TinyUSB の application class driver hook（`usbd_app_driver_get_cb`）から登録し、同梱 source には手を入れていません。この hook は `internal/EspUsbDeviceAppDriver` に置き、CCID クラスが `begin()` で設定するポインタを読むだけにしています。hook から driver を直接名指しすると全スケッチに driver がリンクされる（実測 +2396 byte）ためで、この間接化なら 16 byte で済み、クラスを使うスケッチだけに driver が残ります。message buffer を `begin()` でヒープ確保しているのも同じ理由です。example `SmartCardReader`、peer test `tests/peer/usb_ccid`、および実際の descriptor 生成コードを g++ でコンパイルして class descriptor の全フィールドを検証する host 側 test `tests/unit/ccid_descriptor` を追加しました。
- (EN) Answer audio class GET requests with `min(wLength, payload)` instead of stalling when `wLength` differs from the exact payload size. `wLength` bounds what the host accepts, it is not an exact request, so the previous strict comparison was non-compliant: Linux (`snd_usb_audio`) reads `wNumSubRanges` of the UAC2 sample-rate `RANGE` with `wLength = 2` before asking for the whole list, which made sample-rate discovery fail on every Linux host. `RANGE` now accepts anything from the 2-byte header up, `CUR` accepts any non-zero length, and both truncate the response instead of returning a STALL; the UAC1 `GET_CUR`/`MIN`/`MAX`/`RES` path is relaxed the same way. `SET_CUR` keeps its exact length check, where `wLength` really is the payload size. Reported by EspUsbHost, which had implemented a host-side workaround.
- (JA) Audio classのGET要求に対し、`wLength`が実payloadサイズと一致しない場合もSTALLせず`min(wLength, payload)`を返すようにしました。`wLength`はHostが受け取れる上限であって要求サイズの宣言ではないため、従来の厳密一致は仕様非適合でした。Linux（`snd_usb_audio`）はUAC2のsample-rate `RANGE`を、まず`wLength = 2`で`wNumSubRanges`だけ読んでから全体を要求するので、すべてのLinux Hostでsample-rate取得が失敗していました。`RANGE`は2 byteのheader以上、`CUR`は0以外の任意長を受け付け、いずれも応答を切り詰めて返します。UAC1の`GET_CUR`/`MIN`/`MAX`/`RES`経路も同様に緩めました。`wLength`が実データ長そのものである`SET_CUR`は厳密一致のままです。Host側で回避策を実装済みだったEspUsbHostからの指摘によります。

## 2.0.0
- (EN) Cover device behaviour that only became testable with EspUsbHost 2.7.0 on the host side of the peer rig (the sketches previously pinned 2.5.2). `peer/usb_vendor` adds three tests using APIs from 2.5.3: the bulk pipes `vendorOpen()` actually opened match the endpoints and 64-byte packet sizes the device declares; a write of exactly one full packet followed by an automatic ZLP arrives as one complete 64-byte read and leaves the endpoint usable, which is the device-side half of the packet boundary the P4 manual test covers outbound; and four queued back-to-back full-packet writes all reach the sketch, a burst the synchronous API could not produce. `peer/usb_midi` adds a check that a MIDI-only device enumerates as supported with its AudioControl + MIDIStreaming pair - `EspUsbHostDeviceInfo::supported` only started counting a MIDI interface in 2.6.0, so this device used to enumerate as unsupported.
- (JA) peer rigのHost側がEspUsbHost 2.7.0になって初めてテスト可能になったdeviceの挙動を追加で検証します（sketchの以前のpinは2.5.2）。`peer/usb_vendor`に2.5.3のAPIを使う3件を追加しました——`vendorOpen()`が実際に開いたbulk pipeがdeviceの宣言するendpointと64 byteのpacket sizeに一致すること、ちょうど1 packet分の書き込みと自動ZLPが64 byteの完全な1回の読み出しとして届きendpointがその後も使えること（P4手動testがdevice→host方向でカバーしているpacket境界の、device受信側の半分）、queueで連続投入した4 packetがすべてsketchへ届くこと（同期APIでは作れなかった負荷）。`peer/usb_midi`には、MIDI単機能のdeviceがAudioControl + MIDIStreamingの2 interfaceでsupportedとして列挙されることの確認を追加しました——`EspUsbHostDeviceInfo::supported`がMIDI interfaceを数えるようになったのは2.6.0で、それ以前はこのdeviceがunsupported扱いでした。
- (EN) Add `EspUsbDeviceHidKeyboard::ledState()`, returning the latest LED output report from the host by value. It is updated whether or not an `onOutputReport()` callback is installed, so a sketch can read Lock state even when an integration layer owns the single callback slot (lighting an external Caps Lock LED, for instance) - previously that state was unreachable. LEDs are state rather than an event, so this is a getter and the callback deliberately stays single-slot instead of growing a listener list. It returns by value because the TinyUSB device task writes the state while the sketch reads it from its own task; the raw LED byte is stored atomically and the report is built from one read, so the fields can never describe two different host reports. Matches `EspBleHidKeyboard::ledState()`. `EspUsbDeviceHidKeyboardOutputReport::setLeds()` is the one place that maps bits to flags, so `leds` and the flags cannot disagree; the library never assigns the fields directly.
- (JA) `EspUsbDeviceHidKeyboard::ledState()`を追加しました。Hostからの最新LED output reportを**値で**返し、`onOutputReport()` callbackの有無に関係なく更新されるため、統合レイヤがcallbackの単一slotを占有していてもスケッチ側からLock状態を読めます（外付けCaps Lock LEDを光らせる等）。従来はこの状態に到達できませんでした。LEDはeventではなく状態なので、listener化ではなくgetterとし、callbackは単一slotのままにしています。値返しにしたのは、この状態を書くのがTinyUSB device taskで読むのはスケッチのtaskだからです。raw LED byteをatomicに保持し1回の読み出しからreportを組み立てるので、フィールドが別々のhost reportを指すことはありません。`EspBleHidKeyboard::ledState()`と同形です。`EspUsbDeviceHidKeyboardOutputReport::setLeds()`がビットとフラグの対応を決める唯一の場所で、`leds`とフラグが食い違う状態を作れません（ライブラリ側はフィールドを直接代入しません）。
- (EN) Clear the keyboard's host-facing state (held keys and LEDs) when the USB bus attaches or detaches, via new `tud_mount_cb` / `tud_umount_cb` handling dispatched to `EspUsbDeviceClass::onBusAttached()` / `onBusDetached()` (no-op by default). Without this, a chord held across an unplug survived in the library while the host held nothing, so a state-based caller suppressing duplicate sends against `heldState()` would skip the next send and leave a stuck key. Both hooks are needed: `tud_umount_cb` does not run for a bare bus reset and needs VBUS sensing to see an unplug, while `tud_mount_cb` always runs on re-enumeration. The held-key state is cleared on the sketch's task (the USB task only raises a flag) so it keeps a single writer and `heldState()` can still return a reference.
- (JA) USB busのattach / detach時に、keyboardのHost向け状態（保持キーとLED）をクリアするようにしました。`tud_mount_cb` / `tud_umount_cb`を実装し、新しい`EspUsbDeviceClass::onBusAttached()` / `onBusDetached()`（既定no-op）へ配送します。従来は抜線を挟んでも保持キーがライブラリ側に残るため、Hostは何も押していないのに`heldState()`と比較して重複送信を抑制する呼び出し側が「前回と同じ」と判断して送信を省き、stuck keyになりました。両hookが必要です——`tud_umount_cb`は素のbus resetでは呼ばれず、抜線検出にはVBUS sensingが必要で、常に発火するのは再enumeration時の`tud_mount_cb`だからです。保持キーのクリアはスケッチのtaskで行います（USB taskはflagを立てるだけ）。書き手を1つに保ち、`heldState()`が参照を返せるようにするためです。
- (EN) Add a state-based NKRO send API to `EspUsbDeviceHidKeyboard`: `sendReport(const EspUsbDeviceNkroKeyboardReport &)` puts the whole held-key state on the wire as one report, and `heldState()` returns the state the host was last told about. The new report type carries `modifiers` plus a 28-byte `bitmap` of usages `0x00`-`0xDF` with `clear()` / `press()` / `release()` / `isDown()`; modifier usages `0xE0`-`0xE7` are routed into `modifiers` automatically. Until now the only public route to the bitmap was the incremental `pressUsage()` / `releaseUsage()` API, so a state-based integration layer (which hands over the complete key set every cycle) could not emit more than six keys per report and had to diff against library-internal state, splitting chords into one report per key. Calls without `enableNkro()` fail — that is a setup mistake, unlike the host-driven boot-protocol case, which still folds the state down to the 6-key boot report. The library never suppresses repeated identical states; use `heldState()` to do that in the caller. The existing 6-key `sendReport()` overload and all character helpers are unchanged. Shape-for-shape symmetric with `EspBleHidKeyboard`, so a bridge's USB and BLE output adapters stay near-identical.
- (JA) `EspUsbDeviceHidKeyboard`に状態ベースのNKRO送信APIを追加しました。`sendReport(const EspUsbDeviceNkroKeyboardReport &)`が保持キー全体を1レポートとして送り、`heldState()`がHostへ最後に伝えた状態を返します。新しいreport型は`modifiers`とusage `0x00`-`0xDF`の28-byte `bitmap`を持ち、`clear()` / `press()` / `release()` / `isDown()`で操作します（modifier usage `0xE0`-`0xE7`は自動的に`modifiers`へ振り分けます）。従来はbitmapへ到達する公開経路が増分APIの`pressUsage()` / `releaseUsage()`だけだったため、毎周期にキー集合全体を渡す統合レイヤからは1レポート6キーが上限で、ライブラリ内部状態との差分計算も呼び出し側に必要で、同時押しが1キー1レポートに分割されていました。`enableNkro()`未実行の呼び出しは失敗します（構成の誤りであるため。Host主導のboot protocol時は従来どおり6キーboot reportへ畳んで送ります）。同一状態の再送はライブラリ側で抑制しないので、必要なら`heldState()`と比較して呼び出し側で抑制します。既存の6キー版`sendReport()`と文字helperの挙動は変わりません。`EspBleHidKeyboard`と同一形状なので、bridgeのUSB出力adapterとBLE出力adapterがほぼ同一コードになります。
- (EN) Behaviour change in the NKRO incremental API: `pressUsage()` / `releaseUsage()` now return false for a usage the bitmap cannot represent (above `0xDF` and not a modifier) instead of silently ignoring it; passing a modifier *usage* (`0xE0`-`0xE7`) now presses that modifier instead of being discarded; and `releaseUsage()` now clears modifier usages from the modifier byte, which previously stayed held until `releaseAll()`. The internal held state is now the public report type itself, so the bitmap layout and modifier routing are defined in exactly one place.
- (JA) NKROの増分APIの挙動を変更しました。`pressUsage()` / `releaseUsage()`はbitmapで表現できないusage（`0xDF`超でmodifierでもないもの）に対し、黙って無視するのではなくfalseを返します。modifierの**usage**（`0xE0`-`0xE7`）を渡した場合は破棄せずそのmodifierを押します。また`releaseUsage()`がmodifier usageをmodifierバイトから落とすようになりました（従来は`releaseAll()`まで押されたままでした）。内部の保持状態を公開report型そのものにしたため、bitmapレイアウトとmodifier振り分けの定義が1箇所になります。
- (EN) Adding the NKRO overload makes brace-initialized `sendReport({})` calls ambiguous between the two report types. No example or test in this repository used that form; sketches that do must name the type, e.g. `sendReport(EspUsbDeviceBootKeyboardReport{})`.
- (JA) NKRO版overloadの追加により、`sendReport({})`のようなブレース初期化呼び出しは2つのreport型の間で曖昧になります。本リポジトリのexample / testに該当箇所はありませんが、この形を使っているスケッチは`sendReport(EspUsbDeviceBootKeyboardReport{})`のように型を明示してください。
- (EN) Rename convention across the three sibling libraries: a member holding a bitmap is `bitmap`, a member holding an array of usages is `keys`. `EspUsbDeviceBootKeyboardReport::keys[6]` is a usage array and keeps its name; the new NKRO report uses `bitmap`. `EspUsbHost` renamed `EspUsbHostKeyboardState::keys` / `changedKeys` to `bitmap` / `changedBitmap` in 2.7.0 for the same reason, so no rename is needed here.
- (JA) 姉妹3ライブラリ共通の命名規則です。bitmapを持つメンバは`bitmap`、usageの配列を持つメンバは`keys`とします。`EspUsbDeviceBootKeyboardReport::keys[6]`はusage配列なので名前はそのまま、新しいNKRO reportは`bitmap`です。同じ理由で`EspUsbHost`は2.7.0で`EspUsbHostKeyboardState`の`keys` / `changedKeys`を`bitmap` / `changedBitmap`へ改名しており、本ライブラリ側の改名は不要です。
- (EN) Add the `nkro_report` host-side unit test. It extracts `EspUsbDeviceNkroKeyboardReport` from the real header at run time and compiles it with g++, covering the bitmap layout, `0xE0`-`0xE7` modifier routing, the `MaxBitmapUsage` boundary, ten simultaneous keys, and copy semantics. The `KeyboardNKRO` example now sends its ten-key chord as a single state report.
- (JA) host側unit test `nkro_report`を追加しました。実ヘッダから`EspUsbDeviceNkroKeyboardReport`を実行時に抽出してg++でコンパイルし、bitmapレイアウト、`0xE0`-`0xE7`のmodifier振り分け、`MaxBitmapUsage`境界、10キー同時押し、コピー意味論を検証します。`KeyboardNKRO` exampleは10キーのchordを1つの状態レポートとして送る形へ更新しました。
- (EN) Reduce the `MSCFatRamDisk` example from a 96 KiB to a 64 KiB FAT12 image so it links within ESP32-S2 internal DRAM alongside the library-owned TinyUSB class buffers. The CONFIG.TXT copy/eject/read workflow is unchanged.
- (JA) `MSCFatRamDisk` exampleのFAT12 imageを96 KiBから64 KiBへ縮小し、library所有のTinyUSB class bufferと合わせてもESP32-S2の内部DRAMに収まるようにしました。CONFIG.TXTのcopy/eject/read手順は変わりません。
- (EN) Guard the current DWC2 transfer model with compile-time tests: Device DMA must remain disabled and CPU-driven slave/FIFO mode enabled on S2, S3, and P4. Enabling Device DMA now requires an intentional test update and cache-coherency audit instead of silently exposing cached P4 buffers to a new DMA path.
- (JA) 現在のDWC2転送modelをcompile-time testで固定しました。S2/S3/P4でDevice DMAを無効、CPU駆動のslave/FIFO modeを有効のまま必須とします。Device DMAを有効化する場合はtestの意図的な更新とcache coherency監査が必要になり、P4のcached bufferを新しいDMA経路へ暗黙に晒しません。
- (EN) Add an optional P4-to-PC High-Speed manual test with a dedicated raw Vendor bulk echo sketch and PyUSB host checker. It verifies an actual HS link, 512-byte active bulk endpoints, Device Qualifier, 64-byte Other-Speed bulk endpoints, and configurable sustained byte-for-byte echo without adding hardware-dependent work to the default pytest suite.
- (JA) P4とPCを直結する任意のHigh-Speed手動testを追加しました。専用raw Vendor bulk echo sketchとPyUSB Host checkerで、実HS link、active bulk endpointの512 bytes、Device Qualifier、Other-Speed bulk endpointの64 bytes、指定容量の連続byte一致を確認します。hardware依存のためdefault pytestには追加しません。
- (EN) Make the P4 HS host checker report an actionable Linux/WSL device-permission error instead of a PyUSB traceback, and document temporary node access plus a persistent VID/PID udev rule.
- (JA) P4 HS Host checkerでLinux/WSLのdevice permission不足をPyUSB tracebackではなく具体的な対処として表示し、一時的なnode許可とVID/PID指定の恒久udev ruleを手順へ追加しました。
- (EN) Handle the valid ZLP emitted after each flushed full-MPS P4 HS bulk echo. The checker now skips and counts zero-length terminators while still requiring an exact byte-for-byte payload match.
- (JA) P4 HS bulkでfull-MPSのechoをflushした後に送られる正規のZLPへ対応しました。checkerは0-byte終端を数えて読み飛ばしつつ、payload全体のbyte一致を引き続き必須にします。
- (EN) Reset class endpoints with the standard USB `SET_CONFIGURATION 0 → 1` sequence before a P4 HS run, preventing a payload or ZLP left by an interrupted checker from desynchronizing the next run.
- (JA) 中断したP4 HS checkerが残したpayloadやZLPで次回実行が同期ずれしないよう、開始前にUSB標準の`SET_CONFIGURATION 0 → 1`でclass endpointを再初期化します。
- (EN) Make the default compatibility report filename stable for worktree runs: `--lib-version WORKTREE` now writes `docs/COMPATIBILITY.WORKTREE.md` instead of embedding the current released version as `<version>+wt`. Tagged/ref runs and explicit `--output` paths are unchanged.
- (JA) worktree互換reportの既定file名を固定しました。`--lib-version WORKTREE`は現在のrelease versionを`<version>+wt`としてfile名へ含めず、`docs/COMPATIBILITY.WORKTREE.md`へ出力します。tag/ref実行と明示的な`--output`は従来どおりです。
- (EN) Expand the automatic Arduino-ESP32 compatibility observation range to 3.3.0 while keeping 3.3.9 as the official support floor. Markdown columns below 3.3.9 are labeled `(info)` with an explicit non-support notice, and JSON payloads now carry both floors and per-core support status.
- (JA) Arduino-ESP32互換matrixの自動観測範囲を3.3.0まで広げつつ、公式対応下限は3.3.9のまま維持します。3.3.9未満のMarkdown列には`(info)`と非サポート注記を付け、JSON payloadにも両floorとCoreごとのsupport statusを記録します。
- (EN) Rewrite the user and design documentation around the v2 ownership boundary. The READMEs now explain that the library builds and initializes its own pinned TinyUSB source/configuration, what runtime controller/per-speed descriptor/composite/WebUSB/reinitialization capabilities this enables, and which ESP-IDF/Core dependencies remain. Audio documentation now covers the breaking `EspUsbAudioFunction` + Playback/Capture stream model, bounded polling/event/stats APIs, first-party source boundary, validated UAC1 scope, and explicitly deferred UAC2 streaming.
- (JA) v2のownership境界に合わせて利用者・設計文書を更新しました。libraryが固定TinyUSB source/configurationをbuild・初期化すること、それにより可能になったruntime controller選択、per-speed descriptor、composite/WebUSB、再初期化、残るESP-IDF/Core依存をREADMEへ明記しました。Audioは破壊的変更後の`EspUsbAudioFunction` + Playback/Capture stream、bounded polling/event/stats API、first-party source境界、検証済みUAC1範囲、保留中のUAC2 streamingを説明します。
- (EN) Stop tracking the complete TinyUSB upstream source snapshot. Normal builds keep using the selected 43-file projection in `src/` without network access; `verify_tinyusb_vendor.py` now downloads the pinned upstream commit into an ignored cache only when a byte-for-byte vendor audit is requested.
- (JA) TinyUSB upstream sourceの完全なsnapshotをtracked fileから外しました。通常buildは引き続き`src/`の選択済み43-file projectionだけをnetwork不要で使用し、byte-for-byteのvendor監査を実行するときだけ`verify_tinyusb_vendor.py`が固定commitをignored cacheへ取得します。
- (EN) Define continuous TinyUSB maintenance rules and make `UPSTREAM.json` the single source of truth for the repository, full commit SHA, TinyUSB version, reviewed Arduino-ESP32 baseline, and selection reason. Changing the pin automatically selects a new ignored cache; `update_tinyusb_vendor.py` previews and applies the manifest-selected upstream files. Every accepted update requires the full S2/S3/P4 example matrix, byte-for-byte verification, and complete hardware pytest suite.
- (JA) TinyUSBの継続保守ルールを定め、repository、full commit SHA、TinyUSB version、確認したArduino-ESP32 baseline、選定理由の唯一の正を`UPSTREAM.json`にしました。pinを変えると新しいignored cacheが自動選択され、`update_tinyusb_vendor.py`でmanifest選択済みupstream fileの反映内容をpreview/applyできます。更新を受け入れるにはS2/S3/P4の全example matrix、byte-for-byte検証、完全な実機pytest suiteを必須にします。
- (EN) Expand example compile automation from one hard-coded S3 profile to every profile declared by each example's `sketch.yaml` (68 S2/S3/P4 builds currently). Board-specific M5 examples remain S3-only, while the new P4 port examples compile only for P4.
- (JA) exampleのcompile自動化を、固定のS3 profile 1つから各`sketch.yaml`が宣言する全profile（現在68件のS2/S3/P4 build）へ拡張しました。board固有のM5 exampleはS3のみ、新しいP4 port exampleはP4のみをcompileします。
- (EN) Add `P4HighSpeedDevice` and `P4FullSpeedDevice` examples. They identify rhport 1/UTMI versus rhport 0/internal FS, explain which board connection carries the Device link, and keep the optional `usb_wrap_ll_phy_select(&USB_WRAP, 0)` GPIO26/27-to-GPIO24/25 FS routing change commented in the source. Both examples warn that D+/D- routing does not configure board-specific VBUS or USB-C CC circuitry.
- (JA) `P4HighSpeedDevice`と`P4FullSpeedDevice` exampleを追加しました。rhport 1/UTMIとrhport 0/internal FS、Device linkを接続するboard側port、GPIO26/27既定からGPIO24/25へ切り替える任意の`usb_wrap_ll_phy_select(&USB_WRAP, 0)`をコメントアウトした状態で説明します。D-/D+ routingではboard固有のVBUSやUSB-C CC回路を設定しない注意も記載しています。
- (EN) Extend the one-board P4 HID keyboard loopback to test both valid controller assignments across the same cable: Device HS/rhport 1 + Host FS/rhport 0, then Device FS/rhport 0 + Host HS/rhport 1. Both phases verify Device-to-Host keyboard input and Host-to-Device LED output after runtime teardown and reinitialization.
- (JA) P4 1台のHID keyboard loopbackを、同じcable上の有効な両controller構成へ拡張しました。Device HS/rhport 1 + Host FS/rhport 0の後、runtimeを終了・再初期化してDevice FS/rhport 0 + Host HS/rhport 1を検証します。両phaseでDevice→Host keyboard inputとHost→Device LED outputを確認します。
- (EN) Validate ESP32-P4 endpoint capacity against the selected controller before TinyUSB starts. Full-speed/rhport 0 uses endpoint numbers 1-6 with at most four non-control IN and six OUT endpoints; high-speed/rhport 1 (including P4 `Auto`) uses endpoint numbers 1-15 with at most seven IN and fifteen OUT endpoints. A P4 hardware unit test verifies that a five-IN composite is rejected for FS and accepted for HS/Auto.
- (JA) TinyUSB開始前のESP32-P4 endpoint容量検証を、選択controller別にしました。FullSpeed/rhport 0はendpoint番号1-6、非control IN最大4・OUT最大6、HighSpeed/rhport 1（P4の`Auto`を含む）はendpoint番号1-15、IN最大7・OUT最大15として検証します。INを5本使うCompositeがFSでは拒否され、HS/Autoでは受理されることをP4実機unitで確認します。
- (EN) Restore per-channel USB Audio feature-unit controls. UAC1 and UAC2 descriptors now advertise mute and volume for Master and every logical channel, with independent Master/Left/Right state. `EspUsbAudioFunction` adds Host-aligned `hasMute/getMute/setMute`, `hasVolume/getVolume/setVolume/getVolumeRange` APIs using raw 1/256 dB volume values; host SET requests and local setters share the same state and polling-event path. PCM samples are still never modified implicitly.
- (JA) USB Audio feature unitのチャンネル別controlを復旧しました。UAC1/UAC2 descriptorはMasterと各logical channelにmute/volumeを広告し、Master/Left/Rightごとに独立したstateを持ちます。`EspUsbAudioFunction`へEspUsbHostと語彙を合わせた`hasMute/getMute/setMute`、`hasVolume/getVolume/setVolume/getVolumeRange` APIを追加し、volumeは1/256 dBのraw値を使います。HostのSET requestとlocal setterは同じstate/event queueを共有します。PCM自体への暗黙適用は引き続き行いません。
- (EN) Move WebUSB and Microsoft OS 2.0 descriptor ownership into `EspUsbDevice`. WebUSB-enabled vendor devices now advertise both BOS platform capabilities and answer the Microsoft descriptor-set vendor request with the fixed WinUSB compatible ID and device-interface GUID previously supplied by Arduino-ESP32. The function subset uses the vendor interface number actually allocated in a composite device; WebUSB without a vendor function does not advertise a misleading WinUSB binding. Descriptor unit tests and a P4 control-transfer loopback test cover the complete 178-byte response.
- (JA) WebUSB / Microsoft OS 2.0 descriptor の所有を `EspUsbDevice` 側へ移しました。WebUSB を有効にした vendor device は両方の BOS platform capability を広告し、従来 Arduino-ESP32 が提供していた固定 WinUSB compatible ID / device-interface GUID の Microsoft descriptor-set vendor request に応答します。function subset には Composite で実際に割り当てた vendor interface 番号を使用し、vendor function がない WebUSB 構成では誤った WinUSB binding を広告しません。descriptor unit test と P4 control-transfer loopback test で 178 byte の応答全体を検証します。

## 1.2.7
- (EN) Fix the UAC2 audio build on Arduino-ESP32 core 3.3.11, which removed the `audio20_control_request_t` struct from TinyUSB's `audio.h` (the `AUDIO20_*` constants and `audio20_control_cur_*`/`range_*` types remain). Its layout is fixed by the USB Audio 2.0 spec (§5.2.2) and is a byte-for-byte reinterpretation of the 8-byte setup packet, so `EspUsbDeviceAudio` now defines its own `esp_usb_audio20_control_request_t` and casts `tusb_control_request_t` to it. No version `#if` is needed — the private type name never collides, so it builds on both the older cores that still ship the struct and 3.3.11.
- (JA) Arduino-ESP32 core 3.3.11 で TinyUSB の `audio.h` から `audio20_control_request_t` 構造体が削除された件に対応し、UAC2 オーディオがビルドできるよう修正しました（`AUDIO20_*` 定数や `audio20_control_cur_*`/`range_*` 型は残存）。この構造体は USB Audio 2.0 仕様（§5.2.2）でレイアウトが固定されており、8 バイトの setup パケットをそのまま読み替えたものなので、`EspUsbDeviceAudio` 内で同一レイアウトの独自型 `esp_usb_audio20_control_request_t` を定義し、`tusb_control_request_t` をこれにキャストするようにしました。独自の型名なので名前衝突せず、バージョン `#if` 分岐も不要で、構造体が残る旧 core と 3.3.11 の両方でビルドできます。

## 1.2.6
- (EN) Migrate the keyboard keymap tables to the Unicode 4-plane representation shared byte-identically with EspUsbHost: `KEYCODE_TO_ASCII_XX[N][2]` (`uint8_t`) becomes `KEYCODE_TO_UNICODE_XX[N][4]` (`uint16_t`), with columns `[0]=unshifted, [1]=Shift, [2]=AltGr, [3]=AltGr+Shift`. This also picks up the upstream nl_NL (Windows KBDNE), pt_BR (ABNT2), and ja_jp table/comment fixes.
- (JA) キーボードの keymap テーブルを、EspUsbHost と byte-identical に共有する Unicode 4-plane 表現へ移行しました：`KEYCODE_TO_ASCII_XX[N][2]`（`uint8_t`）を `KEYCODE_TO_UNICODE_XX[N][4]`（`uint16_t`）にし、列は `[0]=unshifted, [1]=Shift, [2]=AltGr, [3]=AltGr+Shift` です。これに伴い上流の nl_NL（Windows KBDNE）・pt_BR（ABNT2）・ja_jp のテーブル/コメント修正も取り込みました。
- (EN) `EspUsbDeviceHidKeyboard` character input (`print()` / `write()` / `pressKey()` / `tapKey()`) now supports the AltGr (Right Alt) layer. `asciiToUsage()` looks up base/Shift first (unchanged precedence, so existing single-level layouts are unaffected), then falls back to the AltGr / AltGr+Shift columns and emits the Right Alt (`0x40`) modifier. AltGr-only characters are now typeable — e.g. `print('@')` on de_DE sends AltGr+Q. Characters outside Latin-1 (e.g. `€` = U+20AC) still cannot be produced from the single-byte `char` API.
- (JA) `EspUsbDeviceHidKeyboard` の文字入力（`print()` / `write()` / `pressKey()` / `tapKey()`）が AltGr（Right Alt）レイヤに対応しました。`asciiToUsage()` はまず base/Shift を探索し（優先順位は従来どおりで、既存の単一レイヤなレイアウトには影響なし）、見つからなければ AltGr / AltGr+Shift 列にフォールバックして Right Alt（`0x40`）修飾を付けて送出します。AltGr 必須の文字が type できるようになり、例えば de_DE で `print('@')` が AltGr+Q を送ります。Latin-1 外の文字（例：`€` = U+20AC）は1バイトの `char` API では従来どおり生成できません。
- (EN) Fix pt_BR so `/` and `?` are typeable: they live on International1 (usage `0x87`) in the ABNT2 layout, so the pt_BR reverse lookup now scans the extended `0x90`-entry table (previously only ja_jp did). Without this `/` incorrectly fell back to AltGr+Q.
- (JA) pt_BR で `/` と `?` を type できるよう修正しました。ABNT2 ではこれらが International1（usage `0x87`）にあるため、pt_BR の逆引きが拡張された `0x90` エントリのテーブルを走査するようにしました（従来は ja_jp のみ）。これがないと `/` が誤って AltGr+Q にフォールバックしていました。
- (EN) Add a host g++ unit test (`tests/unit/keymap`) that extracts the real layout enum, `MOD_*` constants, keymap tables, and the `asciiToUsage()` reverse lookup from the sources and checks the character -> usage+modifier round-trip (base/Shift, the AltGr fallback, and the pt_BR `0x87` fix). Extend the `peer/hid_keyboard_layout` hardware test with de_DE (AltGr) and pt_BR cases that assert the exact usage keycode and modifier byte, not just the resulting glyph.
- (JA) 実ソースから layout enum・`MOD_*` 定数・keymap テーブル・`asciiToUsage()` 逆引きを抽出して、文字 -> usage+modifier の round-trip（base/Shift、AltGr フォールバック、pt_BR の `0x87` 修正）を検証する host g++ 単体テスト（`tests/unit/keymap`）を追加しました。また `peer/hid_keyboard_layout` 実機テストに de_DE（AltGr）と pt_BR のケースを追加し、字形だけでなく usage keycode と modifier バイトまで厳密に検証します。

## 1.2.5
- (EN) Add opt-in N-key rollover (NKRO) to `EspUsbDeviceHidKeyboard` via `enableNkro()` / `nkroEnabled()`. When enabled the keyboard reports a bitmap covering usages `0x00`-`0xDF` (1 modifier byte + 224-bit key bitmap), so any number of keys can be held at once instead of the boot report's 6-key limit. The bitmap range includes International1-9 (`0x87`-`0x8F`) and LANG1-9 (`0x90`-`0x98`), so JIS / non-US layout keys work. The device still folds down to the 6-key boot report when the host selects boot protocol (BIOS). The HID IN endpoint packet size is raised (to 32, within `CFG_TUD_HID_EP_BUFSIZE`=64) — for standalone and composite — so the bitmap report fits one transfer. Default is off, so existing 6KRO behaviour is unchanged. Adds the `KeyboardNKRO` example.
- (JA) `EspUsbDeviceHidKeyboard` に opt-in の N-key rollover（NKRO）を `enableNkro()` / `nkroEnabled()` で追加しました。有効時は usages `0x00`-`0xDF` をカバーする bitmap（modifier 1バイト + 224bit のキービットマップ）で報告するため、boot report の6キー制限なく任意数のキーを同時に押下できます。bitmap 範囲は International1-9（`0x87`-`0x8F`）と LANG1-9（`0x90`-`0x98`）を含むので JIS など US 以外のレイアウト固有キーも通ります。Host が boot protocol（BIOS）を選んだ場合は6キー boot report に畳んで送ります。bitmap レポートが1転送に収まるよう、HID IN エンドポイントの packet size を（単独・複合とも）32 に引き上げました（`CFG_TUD_HID_EP_BUFSIZE`=64 以内）。既定は無効で、従来の 6KRO 挙動は変わりません。`KeyboardNKRO` example を追加しました。

## 1.2.4
- (EN) Fix `EspUsbDeviceNet` (CDC-NCM) to give its own esp_netif a MAC distinct from the advertised iMACAddress. The iMACAddress descriptor is the MAC assigned to the *host* end of the point-to-point USB link; the previous code set the device's own netif to the same value, putting an identical MAC on both ends of one L2 segment (ARP would resolve the peer to the host's own address). The netif MAC is now the advertised MAC with the low bit of the last byte toggled, matching TinyUSB's net_lwip_webserver. The USB link is an isolated segment, so the derived address never leaks onto a real network.
- (JA) `EspUsbDeviceNet`（CDC-NCM）が自身の esp_netif に、広告する iMACAddress とは別の MAC を使うよう修正しました。iMACAddress descriptor は point-to-point な USB リンクの *ホスト側* に割り当てる MAC ですが、従来は自 netif にも同じ値を設定しており、1 つの L2 セグメントの両端が同一 MAC になっていました（ARP が peer をホスト自身のアドレスとして解決してしまう）。自 netif の MAC を、広告 MAC の最下位バイト bit0 を反転した値にしました（TinyUSB の net_lwip_webserver と同じ回避策）。USB リンクは隔離セグメントのため、派生アドレスが実ネットワークに漏れることはありません。

## 1.2.3
- (EN) `EspUsbDeviceNet` (CDC-NCM) now defaults the host-facing MAC to this chip's per-device Ethernet MAC (`esp_read_mac` / `ESP_MAC_ETH`) instead of a single fixed locally-administered address. The Ethernet MAC is derived from the eFuse base MAC and is always distinct from the Wi-Fi STA/AP and BT MACs, so it never collides with the ESP's own Wi-Fi, and two identical boards no longer share a MAC on one host. `macAddress(mac)` still pins a specific address (and then suppresses the auto-derivation). Both the iMACAddress string descriptor and `esp_netif_set_mac` use this value. (Note: two `dhcpServer(true)` devices still share the `192.168.7.0/24` subnet, so multiple boards on one host remains an advanced case.)
- (JA) `EspUsbDeviceNet`（CDC-NCM）のホスト向け MAC を、単一の固定ローカル管理アドレスから **チップ固有の Ethernet MAC（`esp_read_mac` / `ESP_MAC_ETH`）** を既定にしました。Ethernet MAC は eFuse ベース MAC 由来で Wi-Fi STA/AP・BT の MAC とは必ず異なるため、自機の Wi-Fi と衝突せず、同一の 2 枚のボードが 1 台のホスト上で MAC を共有することもなくなります。`macAddress(mac)` を呼べば従来どおり任意の MAC に固定できます（その場合は自動導出を抑止）。iMACAddress string descriptor と `esp_netif_set_mac` の両方がこの値を使います。（注：`dhcpServer(true)` の 2 台は依然として `192.168.7.0/24` サブネットを共有するため、1 台のホストへの複数台接続は上級用途のままです。）

## 1.2.2
- (EN) Add `EspUsbDeviceNet::defaultRoute(bool)` (CDC-NCM). By default the USB netif uses a low route priority (10) so a coexisting Wi-Fi STA (100) stays the ESP's default netif and the USB link never hijacks the ESP's own outbound traffic. Enable it (raises the priority above Wi-Fi) when the ESP should reach the internet *through* the USB host — e.g. a PC that bridges/NATs to it, together with `dhcpClient(true)`. Complements `dhcpAdvertiseGateway()`/`dhcpDns()`, which control the *host's* routing rather than the ESP's.
- (JA) `EspUsbDeviceNet::defaultRoute(bool)`（CDC-NCM）を追加しました。既定では USB netif の route priority を低め（10）にしており、Wi-Fi STA（100）併用時は Wi-Fi が ESP のデフォルト netif のままで、USB 側が ESP 自身の outbound を奪いません。ESP が USB ホスト経由でインターネットに出たい場合（PC がブリッジ/NAT する構成 + `dhcpClient(true)`）に有効化すると priority を Wi-Fi より上げます。ホスト側のルーティングを制御する `dhcpAdvertiseGateway()`/`dhcpDns()` と対になる、ESP 自身のルーティング側の設定です。
- (EN) Harden `EspUsbDeviceNet` (CDC-NCM) after a code review (fixes verified on real hardware): (1) copy each outgoing frame into an internal buffer before `tud_network_xmit` — the actual copy happens later in the usbd task, so handing it the caller's / lwIP's buffer (freed on our return or on TX timeout) was a use-after-free; `sendFrame()` now shares the same mutex-serialized TX path (and its "copied synchronously" comment was wrong); (2) the destructor now clears `g_activeNet` before destroying the netif so `tud_network_*` callbacks stop dereferencing a freed object; (3) the DHCP server no longer advertises a router (option 3) or DNS (option 6) by default — the device is a local endpoint, not a forwarding gateway, so advertising itself as the default route would black-hole the host's off-link traffic; opt in with `dhcpAdvertiseGateway(true)` / `dhcpDns(ip)` when the device really forwards or has a reachable DNS; (4) destroy the esp_netif on a partial `beginNetwork()` failure so a retry does not fail permanently on the duplicate `if_key`; (5) `linkUp()` now reflects `tud_mounted()` instead of latching true after the first configuration.
- (JA) コードレビューを受けて `EspUsbDeviceNet`（CDC-NCM）を堅牢化しました（実機で修正確認済み）：(1) 送信フレームを `tud_network_xmit` の前に内部バッファへコピー（実コピーは後で usbd タスクが行うため、呼び出し側/lwIP のバッファ＝復帰時や送信タイムアウトで解放され得る＝を渡すと use-after-free だった）。`sendFrame()` も同じ mutex 直列化 TX 経路に統一（「同期コピー」というコメントは誤りだったので削除）。(2) デストラクタで netif 破棄の前に `g_activeNet` をクリアし、`tud_network_*` コールバックが解放済みオブジェクトを触らないようにした。(3) DHCP サーバは既定で router(option 3)/DNS(option 6) を広告しない（自 dev は転送しないローカル終端で、既定ルートにされると off-link がブラックホールになるため）。実際に転送する/到達可能な DNS がある場合は `dhcpAdvertiseGateway(true)` / `dhcpDns(ip)` で opt-in 可能。(4) `beginNetwork()` の途中失敗時に esp_netif を破棄し、重複 `if_key` での恒久失敗を防止。(5) `linkUp()` は最初の構成後に true へ張り付かず `tud_mounted()` を反映するようにした。

## 1.2.1
- (EN) Add `EspUsbDeviceNet`, a USB CDC-NCM network device. The board appears to a USB host as a network adapter with no driver install (NCM is native on modern Windows / macOS / Linux). Provides a raw-frame API (`onFrame()` / `sendFrame()`) and optional lwIP/esp_netif integration via `beginNetwork()`, with a built-in, opt-in DHCP server (`dhcpServer()`), DHCP client for a PC-bridged LAN (`dhcpClient()`), or a static address (`ipConfig()`). Verified on a real PC (DHCP lease + `ping`). Adds the `UsbNetwork` example (NCM + DHCP + an HTTP status page at `http://192.168.7.1/`) and the `tests/manual/usb_ncm` manual/pytest test. Device side is NCM only; CDC-ECM is not enabled in the Arduino-ESP32 core. See `docs/DESIGN_NOTES.ja.md` "CDC-NCM ネットワークデバイス".
- (JA) USB CDC-NCM ネットワークデバイス `EspUsbDeviceNet` を追加しました。ボードは USB host からドライバ不要のネットワークアダプタとして見えます（NCM は最近の Windows / macOS / Linux が標準対応）。生フレーム API（`onFrame()` / `sendFrame()`）に加え、`beginNetwork()` による任意の lwIP/esp_netif 統合を提供し、内蔵の opt-in DHCP サーバ（`dhcpServer()`）、PC ブリッジ LAN 用の DHCP クライアント（`dhcpClient()`）、静的アドレス（`ipConfig()`）を選べます。実 PC で DHCP リース取得と `ping` を確認済み。`UsbNetwork` example（NCM + DHCP + `http://192.168.7.1/` の HTTP ページ）と `tests/manual/usb_ncm` の手動/pytest テストを追加。デバイス側は NCM のみ（CDC-ECM は Arduino-ESP32 core で無効）。詳細は `docs/DESIGN_NOTES.ja.md`「CDC-NCM ネットワークデバイス」。
- (EN) Fix composite (multi-function) devices that combine a HID class with non-HID classes, which previously failed to enumerate. Three root causes were found and fixed on real hardware: (1) HID endpoints were not registered in the core's endpoint bitmask, so dynamically-allocated classes (MSC / MIDI / Vendor) collided with them; (2) the HID interface number was baked to 0 and collided with MSC; (3) with a bulk Vendor class present, the HID descriptor loader copied the trailing Vendor interface as well, duplicating it. HID+CDC, HID+MSC, HID+CDC+MSC, and HID+bulk-Vendor now enumerate and function. See `docs/DESIGN_NOTES.ja.md`.
- (JA) HID クラスと非 HID クラスを組み合わせた複合（多機能）デバイスが列挙できなかった不具合を修正しました。実機で 3 つの原因を特定・修正：(1) HID の endpoint が core の endpoint ビットマスクに登録されず、動的採番クラス（MSC / MIDI / Vendor）と衝突、(2) HID の interface 番号が 0 に焼き込まれ MSC と衝突、(3) bulk Vendor 併用時に HID descriptor ローダが末尾の Vendor interface までコピーして二重記述。HID+CDC / HID+MSC / HID+CDC+MSC / HID+bulk Vendor が列挙・動作するようになりました。詳細は `docs/DESIGN_NOTES.ja.md`。
- (EN) Fix `EspUsbDeviceVendor::onRx()` never firing. The library defined `tud_vendor_rx_cb` with the old 1-argument signature, which no longer matches the Arduino-ESP32 TinyUSB 3-argument prototype; being outside `extern "C"` it compiled to a C++-mangled symbol that silently failed to override TinyUSB's weak default. Redefined with the matching signature so the callback fires (data was reaching the RX FIFO, so polling appeared to "work", but the callback was dead). Affected standalone Vendor too, not only composites.
- (JA) `EspUsbDeviceVendor::onRx()` が一度も発火しない不具合を修正しました。ライブラリが旧 1 引数シグネチャで `tud_vendor_rx_cb` を定義しており、Arduino-ESP32 TinyUSB の 3 引数プロトタイプと不一致で、`extern "C"` 外だったため C++ マングル名になって weak default を上書きできていませんでした。シグネチャを合わせて再定義し、コールバックが発火するようにしました（データは RX FIFO に届いていたためポーリングでは動いて見えましたが、コールバックは死んでいました）。複合だけでなく単体 Vendor でも影響していました。
- (EN) Add a `CompositeHidCdcMsc` example: HID keyboard + CDC serial + MSC FAT RAM disk on one `EspUsbDevice`, the richest composite that fits the ESP32-S3 endpoint budget. Add composite peer tests (`composite_hid_cdc`, `composite_hid_msc`, `composite_hid_cdc_msc`, `composite_cdc_msc_vendor`, `composite_hid_vendor`) and a `composite_reject` unit test for the Audio-exclusive / MAX_CLASSES rules.
- (JA) `CompositeHidCdcMsc` example（HID keyboard + CDC serial + MSC FAT RAM disk を 1 つの `EspUsbDevice` に。ESP32-S3 の endpoint 予算に収まる最大の複合）を追加しました。複合の peer テスト（`composite_hid_cdc` / `composite_hid_msc` / `composite_hid_cdc_msc` / `composite_cdc_msc_vendor` / `composite_hid_vendor`）と、Audio 排他 / MAX_CLASSES を固定する `composite_reject` 単体テストを追加しています。
- (EN) Rename the bundled example profile in every `examples/*/sketch.yaml` from `s3` to `esp32s3` (and update `examples_compile` accordingly). Only affects building the examples with `arduino-cli --profile`.
- (JA) 各 `examples/*/sketch.yaml` の example プロファイル名を `s3` から `esp32s3` に改名しました（`examples_compile` も更新）。影響は `arduino-cli --profile` で example をビルドする場合のみです。

## 1.2.0
- (EN) Internal: fold the vendored `USBAudioCard` class into `EspUsbDeviceAudio` as a single self-contained implementation (`src/EspUsbDeviceAudio.cpp`), removing `src/USBAudioCard.{h,cpp}`, the `void*` indirection, the duplicate `UAC_*` enum set, the second singleton, and the `ARDUINO_USB_AUDIO_CARD_EVENTS` translation layer. The descriptor macros move to `src/EspUsbDeviceAudioDescriptors.h`. No public API change: `EspUsbDeviceAudio` and its methods behave identically, including the 1.1.1 dedicated-event-loop crash fix. (Espressif Apache-2.0 attribution is preserved on the derived file.)
- (JA) 内部変更: vendored な `USBAudioCard` クラスを `EspUsbDeviceAudio` に畳み込み、単一の自己完結実装（`src/EspUsbDeviceAudio.cpp`）にしました。`src/USBAudioCard.{h,cpp}`、`void*` 経由の間接参照、重複していた `UAC_*` enum、二重 singleton、`ARDUINO_USB_AUDIO_CARD_EVENTS` の変換層を削除。descriptor マクロは `src/EspUsbDeviceAudioDescriptors.h` へ移動。公開 API は不変で、`EspUsbDeviceAudio` とそのメソッドの挙動は 1.1.1 の専用イベントループによるクラッシュ修正を含め同一です（Espressif の Apache-2.0 帰属表示は派生ファイルに保持）。
- (EN) **Breaking:** rename the USB Audio class `EspUsbDeviceAudioSink` to `EspUsbDeviceAudio`, since the one class covers both directions (speaker sink and microphone source, and both at once). The `AudioSink` / `AudioSinkM5Speaker` examples become `AudioSpeaker` / `AudioSpeakerM5`, the new microphone example is `AudioMicrophone`, and the peer tests `usb_audio` / `usb_audio_mic` become `usb_audio_speaker` / `usb_audio_microphone`. Update sketches to the `EspUsbDeviceAudio` type name; the constructor signature and methods are unchanged.
- (JA) **破壊的変更:** USB Audio クラス `EspUsbDeviceAudioSink` を `EspUsbDeviceAudio` に改名しました。1つのクラスが speaker sink と microphone source の両方向（同時も）を担うためです。example `AudioSink` / `AudioSinkM5Speaker` は `AudioSpeaker` / `AudioSpeakerM5` に、新規マイク example は `AudioMicrophone` に、peer テスト `usb_audio` / `usb_audio_mic` は `usb_audio_speaker` / `usb_audio_microphone` に改名しています。スケッチの型名を `EspUsbDeviceAudio` に更新してください（コンストラクタ引数・メソッドは不変）。
- (EN) Add an `AudioMicrophone` example and a `peer/usb_audio_microphone` test for the USB Audio source (microphone) direction (`writeMic()` / device -> host PCM): the example streams a generated 440 Hz tone as a mono 48 kHz / 16-bit recording device, and the peer test verifies the host receives non-silent PCM (S3, UAC1).
- (JA) USB Audio source（マイク）方向（`writeMic()` / device -> host PCM）の `AudioMicrophone` example と `peer/usb_audio_microphone` テストを追加しました。example は mono 48 kHz / 16-bit の録音デバイスとして 440 Hz トーンを送出し、peer テストは Host が無音でない PCM を受信することを検証します（S3, UAC1）。
- (EN) Add an `AudioHeadset` example and a `peer/usb_audio_headset` test for a USB Audio device that is a speaker and a microphone at once (host <-> device, both directions). The example is a loopback headset (received speaker PCM is echoed back to the mic); the peer test confirms both an OUT and an IN stream enumerate and start, the host's speaker PCM reaches the device, and the device's mic PCM reaches the host and is non-silent (S3, UAC1).
- (JA) speaker と microphone を同時に担う（Host <-> device の両方向）USB Audio デバイスの `AudioHeadset` example と `peer/usb_audio_headset` テストを追加しました。example は loopback headset（受信した speaker PCM を mic へ返す）で、peer テストは OUT と IN の両ストリームが enumerate/開始でき、Host の speaker PCM が device に届き、device の mic PCM が Host に届いて無音でないことを検証します（S3, UAC1）。
- (EN) Add M5-backed audio examples: `AudioMicrophoneM5` (streams `M5.Mic` capture to the host as a mono 16 kHz recording device) and `AudioHeadsetM5` (M5 speaker playback + M5 mic capture). Both use M5Unified; `AudioHeadsetM5` also uses PCMFlow / PCMFlowDevice for playback. Note: simultaneous speaker+mic (full duplex) on M5 is a documented known limitation — M5Unified drives both through one I2S port installed TX-only / RX-only, so concurrent use is unstable (confirmed on CoreS3); `AudioHeadsetM5` is a best-effort demo, with `AudioSpeakerM5` / `AudioMicrophoneM5` for reliable single-direction use.
- (JA) M5 対応の audio example を追加しました：`AudioMicrophoneM5`（`M5.Mic` の取り込みを mono 16 kHz の録音デバイスとして Host へストリーム）と `AudioHeadsetM5`（M5 スピーカー再生＋M5 マイク取り込み）。いずれも M5Unified を使用し、`AudioHeadsetM5` は再生に PCMFlow / PCMFlowDevice も使用します。注記：M5 でのスピーカー＋マイク同時使用（全二重）は既知の制約として非対応です（M5Unified が両方を1つの I2S ポートで TX 専用/RX 専用に install するため同時利用は不安定。CoreS3 でも確認）。`AudioHeadsetM5` はベストエフォートのデモで、安定動作が必要な場合は単方向の `AudioSpeakerM5` / `AudioMicrophoneM5` を使用してください。

## 1.1.1
- (EN) Fix a USB Audio crash where a rapid burst of volume/mute changes (e.g. dragging the Windows volume slider) rebooted the device. The audio control-transfer callback ran the user `onEvent()` on the 2048-byte Arduino USB event-loop task and overflowed its stack. Audio events now dispatch on a dedicated event loop with a generous stack, the event post is non-blocking (drops under overload instead of blocking the USB task), and the feature-unit channel index is bounds-checked. Adds a `peer/usb_audio` volume/mute flood regression test.
- (JA) USB Audio の音量/ミュートを高速連打（Windows の音量スライダードラッグ等）するとデバイスが再起動するクラッシュを修正しました。audio のコントロール転送コールバックがユーザーの `onEvent()` を 2048 バイトの Arduino USB イベントループタスク上で実行し、スタックオーバーフローしていました。audio イベントを大きめのスタックを持つ専用イベントループで配送し、ポストをノンブロッキング化（過負荷時は USB タスクを止めず破棄）、フィーチャーユニットのチャンネル番号に境界チェックを追加しました。`peer/usb_audio` に音量/ミュート連打の回帰テストを追加しています。

## 1.1.0
- (EN) **Breaking:** remove `EspUsbDeviceConfig::port` / `speed` and the `EspUsbDevicePort` / `EspUsbDeviceSpeed` enums. The device no longer selects its USB port/speed: on ESP32-P4 the Arduino core fixes the device to the high-speed (UTMI) controller and the actual link speed is negotiated with the host. Remove any `config.port` / `config.speed` assignments from sketches.
- (JA) **破壊的変更:** `EspUsbDeviceConfig::port` / `speed` と `EspUsbDevicePort` / `EspUsbDeviceSpeed` を削除しました。デバイスは USB ポート/速度を選択しません（ESP32-P4 では Arduino core がデバイスを High Speed(UTMI) コントローラに固定し、実速度はホストとのネゴで決まります）。スケッチから `config.port` / `config.speed` の代入を削除してください。
- (EN) Add the `tests/loopback/hid_keyboard_layout` test, verifying that EN_US / JA_JP symbol keys round-trip through the host on a single ESP32-P4.
- (JA) `tests/loopback/hid_keyboard_layout` を追加し、ESP32-P4 1台で EN_US / JA_JP の記号キーがホストまで往復することを検証します。
- (EN) Define USB Audio on ESP32-P4 as UAC2 / high-speed only, and remove the P4 loopback audio test: one-board loopback runs at full speed (single UTMI PHY), which cannot carry a UAC2 descriptor. Audio stays covered by the ESP32-S3 peer test (UAC1) plus manual high-speed checks.
- (JA) ESP32-P4 の USB Audio を UAC2 / High Speed 専用と定め、P4 の loopback audio テストを削除しました。1台 loopback は Full Speed（UTMI PHY は1個）で UAC2 記述子を扱えないためです。Audio は ESP32-S3 の peer テスト（UAC1）と実機 HS 手動確認でカバーします。
- (EN) Document ESP32-P4 USB port/PHY behavior and a known limitation: endpoint sizes are currently full-speed-fixed (bulk 64), so a P4 device on a real high-speed host is non-compliant for bulk (HS requires 512). The proper fix is per-speed descriptors, deferred to a real-PC / high-speed milestone. See `docs/DESIGN_NOTES.ja.md`.
- (JA) ESP32-P4 の USB ポート/PHY 挙動と既知制約を明記しました：endpoint サイズは現状 Full Speed 固定（bulk 64）で、実 High Speed ホストでは bulk が非準拠になります（HS は 512 必須）。正しい解決は per-speed descriptor で、実 PC / HS 対応マイルストーンに先送りします。詳細は `docs/DESIGN_NOTES.ja.md`。

## 1.0.0
- (EN) Initial release
- (JA) 初期リリース
