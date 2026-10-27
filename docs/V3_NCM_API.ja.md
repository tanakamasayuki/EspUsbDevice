# v3 NCM: Function単位のNTBと任意のIP層

2026-10-27。NCM class、固定NTB pool、MACと送信queueのglobalを独自driverへ移す。
Device + Net + onFrame/sendFrame + beginの利用手順は維持する。共通EP0/core/DCDは暫定TinyUSB。

```cpp
EspUsbDevice device;
EspUsbDeviceNet net(device);
void setup() {
  EspUsbNetConfig config;
  config.ntbInBytes = config.ntbOutBytes = 3200;
  config.txBuffers = 2;
  config.rxBuffers = 1;
  config.name = "USB network";
  if (net.configure(config) != ESP_OK) return;
  net.onFrame([](const uint8_t *frame, size_t bytes) {
    net.sendFrame(frame, bytes); // copy; check false when the queue is full
  });
  device.begin();
}
```

NTBはUSB上で複数のEthernet frameをまとめる転送単位。INはDevice→Host、OUTはHost→Device。
ntbInBytes/ntbOutBytesはそれぞれ2048..65535 byte、既定3200。
OUTは実controllerのMPSへ丸めたarm長が16 bitに収まる必要があり、FSで最大65472、HSで65024 byte。
HS用に確保した領域はFS fallbackにも使う。実機確認はS3のFSのみである。
maxFrameBytesはEthernet headerを含む14..NTB容量−28 byte、既定1514。
maxDatagramsはNTB内の最大frame数で既定8。NTB headerと最大frameが入る範囲で実効値を制限する。
HostがNTB入力容量を再交渉した場合も、その容量とdatagram数の上限を守る。

既定はTX 3 buffer、RX 2 buffer。0ならその方向のpoolを確保せず、frame送信/受信を行わない。
interface/endpointと必須の通知buffer、Functionの管理情報は残る。
TXとRXのpoolはそれぞれ一回確保し、送受信中はheapを使わない。
従来の1600 byte × 4件の別送信queueと永続FreeRTOS queueは撤去し、同じNTBへ直接copyしてworkerへ渡す。
ntbInCapacity/ntbOutCapacityは確保した方向の論理NTB byte数、txBufferCount/rxBufferCountはbuffer数。
begin前/終了後/無効方向では0を返し、configurationは要求値を保持する。

外部txBuffer/rxBufferも指定できる。それぞれのtxBufferBytes/rxBufferBytesを明示する。
内部DMA可能RAM、64 byte alignmentが必要で、TX strideはntbInBytesを64へ、RX strideは
ntbOutBytesをcontrollerのMPSへ切り上げる。必要長はstride × buffer数。
たとえばFSで3200 byte・2 bufferなら各6400 byte、HSのRXは各3584 × 2 byte。
両poolが重なる指定、不足長、alignment/領域の不適合、pointer/size overflowは拒否する。
開始から終了までbufferを有効に保ち、アプリは直接書き換えない。

## frameの寿命と設定変更

sendFrameは全frameをcopyできればtrue、link未開始・queue満杯・長さ不適合ならfalse。
trueはUSBでの配送完了を意味しない。稼働前に送ったframeを後の接続へ持ち越さない。
送信待ちNTBに余地があればframeをまとめ、workerだけがDMAを投稿する。
NTBがMPSの整数倍で交渉上限より短い場合は、末尾ZLPで転送を終える。

onFrameはUSB worker上で呼ぶ。pointerはcallback中だけ有効で、後で使う場合はcopyする。
RX bufferが複数なら次のDMAを先にarmし、一つならcallbackの終了後に同じ領域を再利用する。
受信NTB全体のheader/table/payload範囲と重複を検証してからframeを渡し、途中までの配送をしない。
無効NTBは捨ててinvalidNtbsを増やし、次の正常NTBを受け付ける。
statsのtxFrames/rxFrames/invalidNtbsはbeginからendまでの累積値。TXはUSB完了後に加算する。

configure、macAddress、onFrameは停止中だけ受け付ける。稼働中はInvalidState。
不適合サイズはInvalidSize、外部領域やMAC不正はInvalidArg、取得失敗はstartError/Device.lastErrorのNoMemory。
Device.end→configure→Device.beginで再設定でき、build_opt.hやclean rebuildは不要。
setterはvoidからesp_err_tを返す方式へ変更する。普通の呼出し方は保ち、戻り値を確認できるようにする。

alt変更と稼働中のNTB容量変更は旧DMAをabortし、旧完了eventをFIFO fenceで排出してから領域を再利用する。
待ちframeは破棄する。SET_INTERFACEでは対象endpointのDATA PIDも初期化する。
NTB容量のclass要求ではDATA PIDを保持する。停止時はUSB worker/DMAの終了後に全poolを解放する。

## MACとIPを分ける

MACはFunctionごとの値とUTF-16 snapshotを持ち、固定string index 4を使わない。
既定値はESP MACから導くlocally administered unicastで、複数Function間を分ける。
明示MACはunicastかつ非zeroを受け付ける。onFrame/sendFrameだけならIP用の領域と処理はリンクしない。

beginNetworkを呼んだFunctionだけにesp_netif glueを確保する。netif/if_key/IP情報を共有しない。
IP側は通常の1500 byte MTUなので、maxFrameBytes≥1514と両方向bufferを必要とする。
従来のIP/DHCP/route設定を保ち、netif稼働中の変更はInvalidStateで拒否する。
networkUpはSDK netifの状態、linkUpはUSB data alternateの状態を返す。
endNetworkでIPだけを停止し、同じUSB上のraw frame callbackへ戻せる。Device.endでも回収する。
beginNetwork/endNetworkはアプリtaskから呼び、USB workerからは拒否する。
netif受信は従来同様、lwIPが所有するframe copyを別途確保する。IP利用時の動的費用は残る。

[NCM 1.0 Errata 1の仕様一式](https://www.usb.org/sites/default/files/NCM10_012011.zip)を基準とする。
NTB16・CRCなしを扱い、NTB32、CRC、IPやfilter機能の先行拡張はしない。
Ethernet packet filterのcapabilityは宣言せず、通常のHostが送る設定要求は互換上受け付ける。
Host 2.10.1のNTB容量capability判定は仕様のD5と異なるが、Deviceの宣言は仕様に合わせる。
同HostはDeviceの最大値に追従できるため、実試験で容量とpayloadの一致を確認する。

利点は未使用時の固定pool排除、方向/容量ごとの費用、同じbinaryの構成変更、独立MACとnetifの回収。
不利益はbegin時の確保失敗、copy/lockとmetadataの費用、再交渉で待ちframeを保持しない点。
従来のbegin前送信や、稼働中のMAC/設定変更に依存したコードは移行する。
複数NCMでもcontrollerのendpoint数制限は残り、S3でVendor + 2 NCMはIN endpointが不足する。
PC/OSのNCM driver、DHCP option内容/route選択、P4/S2/S31/HS、外部callbackでの長時間処理は未検証として残す。


## S3–S3の実測

`ncm-driver-related.xml`は**3 passed、0 skipped、0 unfinished、143.77秒**。
NTB容量2048/3200/8192、1/2/3 buffer、外部pool、14..1514 byte frame、ZLP、まとめ送り、
容量再交渉と不正要求後の復帰、heap回復と605取得点の確保失敗/回復を含む。
手動Hostで22種類の不正NTBを捨てて直後の正常frameを返し、block長0、末尾NDP、逆順datagram、
8128 byteのhardware armを跨いだpayload、独立2 NCMとMAC、12回のalt切替とPID初期化を確認した。
serial原本は`/tmp/pytest-embedded/2026-10-26_20-17-53-324449/`。

`ncm-ip-related.xml`は**2 passed、0 skipped、0 unfinished、154.11秒**。
3構成ともDHCPで192.168.7.2を取得しTCPの8193 byteが一致、IP停止後にraw通信へ戻れた。
2 NCMの別IP/keyでの同時作成・片方停止・再作成も成功した。
serial原本は`/tmp/pytest-embedded/2026-10-27_00-57-48-429795/`。
両実行とも各case両board STOPPED、panicなし。コピーとJUnit/transcriptは
`validation/.pytest-results/ncm-driver-related*`と`ncm-ip-related*`。
Netのみ/Deviceのみの最小build/nmも成功し、旧TinyUSB NCMと未使用NCM/IP glueの不在を確認した。
通常収集は19件。関連試験の後に行った一括試験の結果は下記を参照。


## NCM置換後の全体確認

2026-10-27。標準`--combo all`は**19 passed、0 skipped、0 unfinished、1080.50秒**。
全19 caseで両board STOPPED、38本のserialにpanicなし。NCMのraw/control/2 interface、
DHCP/TCPとIP回収、605取得点の確保失敗/回復、および既存の全USB経路を含む。
serial原本は`/tmp/pytest-embedded/2026-10-27_01-02-46-482679/`、コピーとJUnit/transcriptは`validation/.pytest-results/runtime-ncm-all*`。
NCMのみ/Deviceのみの最小build/nmも成功し、旧TinyUSB NCMと未使用NCM/IP glueの不在を確認した。
TinyUSB treeは51 file / 15 compiled sourcesで一致。旧tests/examplesはmainと同一で、mainはv3の祖先。
残るlegacy classはVideo。共通EP0/core/DCDと他SoC/HSの置換・実機検証は未完。
