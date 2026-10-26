# v3 CDCと共通bulk stream API

2026-10-26。CDC-ACMのクラス処理を独自実装へ移し、TinyUSB CDCを無効化した。
VendorとCDCは`EspUsbDeviceBulkStream`のring、DMA、借用・返却、取消しを共有する。
共通EP0、device state、DWC2は引き続き暫定TinyUSB adapterである。

## 普通の利用

```cpp
EspUsbDevice device;
EspUsbDeviceCdcSerial console(device, "console");

void setup() {
  EspUsbBulkStreamConfig config;
  config.rxCapacity = 1024;
  config.txCapacity = 2048;
  config.transferBytes = 256;
  if (console.configure(config) != ESP_OK) return;
  if (!device.begin()) return;
}

void loop() {
  if (console.available()) {
    const int value = console.read();
    if (value >= 0) console.write(uint8_t(value));
  }
}
```

DeviceとFunctionを作り、設定してbegin、read/write/Print、endという流れは維持する。
configureとcallback登録は停止中だけ成功する。容量はinstance単位で、end→configure→beginで変更する。
開始失敗時はdevice.lastError()を確認する。再開のためにbuild_opt.hやclean rebuildを使わない。
設定の意味とborrowed転送の返却契約は[Vendorガイド](V3_VENDOR_API.ja.md)と共通である。

| API/設定 | 意味 |
| --- | --- |
| EspUsbBulkStreamConfig / EspUsbBulkRxMode | Vendor/CDC共通の容量・外部ring・Stream/Callback/Borrowed設定 |
| readCapacity / writeCapacity | 確保済みの実効容量。開始前/終了後は0 |
| configuration | 要求した設定。FunctionDefaultなどの指定を保持 |
| mounted | USB configurationを受けてendpointが開いた状態 |
| connected | mountedかつDTR。writeはDTRを強制条件にしない |
| lineCoding / lineState | workerとの競合を避ける値のsnapshot。呼び直して最新値を取得 |
| onLineCoding / onLineState / onBreak | 停止中に登録。USB workerで通知し、block/end/構成変更を行わない |
| setName | 停止中にUTF-8を設定。各beginが戻るまで入力を保持し、descriptorはその時点のcopyを使う |
| port | beginで割り当てるCDC descriptor順のindex。TinyUSBのslotではない |

`EspUsbVendorConfig` / `EspUsbVendorRxMode`は共通型のaliasとして残す。
通常のVendor/CDC利用で共通baseを直接扱う必要はない。

## コピー送信の終端

`streamTermination`は`EspUsbStreamTermination`で指定する。

- `FunctionDefault`: VendorはContinue、CDCはEndWithShortPacket。
- `Continue`: ringが空になっても追加ZLPを送らない。
- `EndWithShortPacket`: コピー送出がMPS倍数で終わり、ringが空ならZLPまで送る。

CDCの既定値は、Hostの大きなreadへ少量のserial dataを渡せる動作を保つためである。
Vendorは従来のv3のContinueを維持し、必要なら同じ設定で明示できる。
ZLPが不要な連続streamではContinueを選べる。ZLPは一回のwriteをメッセージ境界にする契約ではなく、
複数writeが蓄積されていればまとめて送出される。flushは送出開始で、完了待ちではない。
ZLP送出中に次の送信を割り込ませず、コピー完了通知へ0 byte通知を追加しない。
借用TXの`EspUsbTransferTermination`は転送単位の明示指定のままで、stream設定とは別である。

## 借用転送と未使用資源

CDCもwriteDirect/readDirect、方向別完了callback、cancelDirectTx/cancelDirectRxを使える。
投稿結果と完了のid、成功/取消し/reset/end、DMA停止後の一度だけの返却はVendorと共通。
RXをBorrowed、rxCapacity/txCapacity/transferBytesを0にすれば、copy用ring/packetを確保しない。
CDC固有にはline coding用の専有DMA/cache領域64 byteをbegin時に確保し、endで解放する。
未使用CDCの固定instance配列、FIFO、endpoint bufferはリンクしない。

## 変更の影響

- `CFG_TUD_CDC`や`CFG_TUD_CDC_*_BUFSIZE`で容量/個数を指定する処理は移行対象。
  TinyUSB CDCの再有効化は拒否する。容量はFunctionのconfigureへ移す。
- `onLineCoding/onLineState/onRx/setName`はesp_err_tを返す。戻り値を無視していた呼出しは形を維持できるが、
  稼働中の変更はエラーになるため開始前へ移す。
- lineCoding/lineStateの戻り値は参照から値へ変更。取得済み値が後から変わることを前提にせず、再取得する。
- `Device::maxCdcPorts()`は廃止。個数だけの固定上限では他Functionとのendpoint競合を表せない。
  構成全体をbeginで検証し、失敗理由を確認する。現在の固定Function registry上限は別途撤去予定。
- SEND_BREAKはonBreakで期間を通知する。0/0xffff等の意味はUSB CDCの要求値のまま渡す。

利点はruntime設定、未使用資源の削減、同じ所有権・エラー契約、callback登録時期の統一。
不利益はbegin時の確保失敗、停止してからの再設定、借用buffer寿命管理、snapshot取得への移行である。

## 実験と残り

S3–S3の新規`validation/s3/cdc_stream`で、2 CDCの容量を小/大/非power-of-twoへ同一binaryで変更し、
両portの1/63/64/65/127/128/129 byte、line codingの独立性、借用RX/TXを確認した。
S3の非制御IN endpointは4本なので、2 CDCとCDC+Vendor+HIDは別の構成で試験する。
後者ではCDC/Vendor通信に加えてHID keyの押下・解放まで確認する。初回は1 passed、45.53秒。

これはWindows/Linux/macOSのCDC相互接続や、通知endpointの全機能、全control要求の合格ではない。
HID自体の独自化、動的registry/descriptors、残クラスとcore/DCDの全置換、P4/S2/S31の実機試験は継続する。

最終の標準実行は`cdc-vendor-all.xml`、6 passed、340.68秒。CDCを含む46取得点の確保失敗/回復と、
既存v3の取消し・reset/end・大きな転送・Vendorの明示コピー終端も含む。
Device only / Vendor copy / CDC copyのnmで未使用Function固有処理・取消し・TinyUSB CDC/Vendorが残らないことを確認した。
RXを完全に無効化する送信専用modeの明示と、固定registry/descriptor領域の撤去は次の作業である。

## RXを使わない送信専用構成

`rxMode=EspUsbBulkRxMode::Disabled`、`rxCapacity=0`を指定する。RX callbackは不要で、
RX ring/packetを確保せず、OUTをarmしない。USB descriptorの必須OUT endpointは残る。
`available/read`は0、`maxDirectReceive()`は0、`readDirect()`はNotSupportedを返す。
TXは従来通りcopyまたはborrowedを選べる。借用TXはTX完了callbackが必要。
これはBorrowed RXの未投稿状態とは異なり、受信操作自体を使わない明示設定である。
