# v3 Remote wakeup

休止（suspend）中のHostを、Device側から起こす機能。2026-10-29にS3–S3で実装し、確認した。

次の三つを別々に扱う。

| 段階 | 誰が決めるか | API |
| --- | --- | --- |
| 能力の広告 | アプリ（`begin()`の前） | `EspUsbDeviceConfig::remoteWakeupSupported`（既定false） |
| 使用の許可 | Host（SET_FEATURE / CLEAR_FEATURE） | `remoteWakeupEnabled()`で照会 |
| 起こす要求 | アプリ | `remoteWakeup()` |

## 使い方

```cpp
EspUsbDeviceConfig config;
config.remoteWakeupSupported = true;  // 既定false。begin()でdescriptorに反映する
device.begin(config);

// loop()などのtaskで、外部のイベントがあったとき：
if (device.suspended() && device.remoteWakeupEnabled()) {
  esp_err_t result = device.remoteWakeup();
  // ESP_OKは「要求をqueueに受け付けた」という意味。
  // 実際に復帰したかは、suspended()がfalseになることで確かめる。
}
```

## 契約

### `remoteWakeupEnabled()`

- 現在の構成に対して、Hostが許可しているかを返す。
- bus resetや`end()`でfalseに戻る。runtimeが停止を始めた後もfalse。
- taskから呼ぶ。ISRから呼ぶとfalseを返す。
- Hostの許可をアプリが自分で設定するAPIはない。

### `remoteWakeup()`の戻り値

| 戻り値 | 条件 |
| --- | --- |
| `ESP_OK` | 要求をqueueに受け付けた。復帰の完了ではない |
| `ESP_ERR_INVALID_STATE` | 開始前/停止中、`DescriptorsOnly`、未構成、休止していない、Hostが許可していない、ISRから呼んだ、すでに要求が保留中 |
| `ESP_ERR_TIMEOUT` | event queueに空きがない |
| `ESP_ERR_NOT_SUPPORTED` | USB OTGのないtarget |

- USB workerからの呼出しもできる。
- 受け付けた後に、resume、CLEAR_FEATURE、reset、再構成、停止が起きて不要になった要求は、実行しない。

## 実装の方式と理由

- **常駐の資源を増やさない**：timer、task、専用の保留領域は作らない。既存のworkerとqueue、Hostの許可byte、構成世代を使う。
- **idleの待ち**：USBは、busが5 ms以上idleになった後でないとresumeを要求できない。
  そこで要求の処理時に5 ms＋1 tickを`vTaskDelay`で待ち、状態をもう一度確かめてから信号を出す。
  時刻を記録する領域が要らない。
- **信号の長さ**：USBはDeviceのresume信号を1〜15 msと定めている。
  controllerのlockの中で、ROMの`esp_rom_delay_us(2000)`（2 ms）を使う。
  - 理由：`vTaskDelay(pdMS_TO_TICKS(1))`はtickの途中で呼ぶと1 ms未満になり得るため。
  - 代償：使ったときに、実行coreの割込みが約2 ms遅れる。他の時間制約との調整はアプリが判断する。
    使わなければこの待ちは起きない。
- **停止との競合**：公開の要求と照会はcontroller lockの中でcoreの生存を確かめる。
  停止側も同じlockで拒否状態を公開してから、coreを切り離す。
  停止を始める前にすでに出ている信号を、途中で打ち切る契約ではない。
- これはbusを復帰させる要求で、SoCのsleepやPCの電源管理、電気的な適合性まで確かめたものではない。

## 検証の記録

### 基本試験（S3–S3）

1 passed、50.20秒、2 logs STOPPED、panicなし。

- 能力なしでのSET_FEATUREの拒否。
- 11種類の不正な状態での要求。6回の「信号を出さない」確認。
- Device起因のwake 3回。10回の双方向payload。
- CLEAR_FEATURE後の拒否と、Hostの再初期化（bus reset）後の許可の消去。
- Hostのwake status、SOFのframe進行、Deviceの休止解除と実通信を合わせて確認した。

試験用Hostの注記：最初のHostはclock gateを含まない休止手順で、frameが再開せず失敗した。
固定したSDKは変えず、試験用Hostにclock gate/ungateを含む休止・復帰と、IRQ policyの保存/復元を実装して成功した。
記録は`validation/.pytest-results/remote-wakeup-{first,diagnosed,armed,auto,clock}*`。

### 境界試験

1 passed、45.24秒、2 logs停止、panicなし。

- workerを止めて要求をqueueに入れたままbus resetを入れ、再列挙後の許可の消去と通常通信を確認した。
- 停止前の重複要求も拒否した。
- 停止待ちの間に17回、停止完了後に32回、別taskから要求/照会し、すべて拒否/falseになった。
- 修正前は、停止待ちの間の照会がtrueのままで失敗した。既存の`g_stop`で受付と実行を抑止して直した。

記録は`remote-wakeup-{reset,boundaries,stop-before,stop-after}*`。

### 全体試験と他SoC

- 停止guardの後（`80e8676`）、全47件が1917.29秒で成功。94 logs全停止、panicなし。
  実wake 3回、信号なし7回、10往復、停止中17回・停止後32回の拒否を含む。記録`remote-all-audit.txt`。
- S2/P4はcompileのみ：機械種別、archive、29 sourceの依存、pulse/IO/delayの出所、未選択classの除外を確認した。
  記録`remote-final-cross-audit.json`。

### 未使用時の費用

S3の最小probe（記録`remote-final-unused-audit.json`）：

| probe | Flash | global |
| --- | --- | --- |
| includeのみ | 追加0 | 追加0 |
| Device宣言のみ | 261957 byte | 21840 byte |

旧archive基準（261913/21824）より+44/+16 byte。Device objectは196 byteのまま変わらず、共通の生存確認lockが新たにリンクされた分。
未使用のwake要求、pulse、worker/PHYの開始処理、他classの状態は含まれない。停止guardの後も同じ値。

### 未確認

- S2/P4の実機でのremote wakeup。
- PCのOS（Windows/Linux/macOS）での動作。

## 実装前の調査記録（2026-10-29）

以下は実装前に調べた内容。「候補」と書いた方式は、上の[実装の方式と理由](#実装の方式と理由)のとおり採用した。

### 規格と状態

[USB 2.0原典の保存版](https://bitsavers.trailing-edge.com/components/usb/USB_2.0_2000.pdf)の
§7.1.7.7（本文156頁）、§9.2.5.2、§9.4.5（255頁）を確認した。
公式配布の入口は[USB-IF](https://www.usb.org/document-library/usb-20-specification)。

- 能力のdescriptorでの広告と、HostのSET/CLEAR_FEATUREによる許可は別の状態。
- 許可の既定は無効で、bus resetで消える。GET_STATUSで現在の許可を返す。
- Deviceがresumeを要求できるのは、busが連続5 ms idleになった後。
- Deviceのresume信号は1～15 ms。Hostが引き継ぐresume処理と、Deviceの信号時間を混同しない。

当時の候補は、configの能力設定（既定false）、Host許可の照会、要求APIの3点。
要求の成功を「Hostが復帰済み」と扱わず、復帰は`suspended()`で観測する。

当時のDWC2の`espusb_controller_remote_wakeup`は、IOの有効性や寿命を確かめずにDCTLを操作していた。
公開する前に、上位の状態判定と、controllerの停止・resetとの同期が必要だった。
platformの`vTaskDelay(pdMS_TO_TICKS(1))`は「1 ms以上の信号」を保証しない。
S3 SDKは`CONFIG_FREERTOS_HZ=1000`だが、tickの途中で呼ぶと1 ms未満になり得る。

### ピン留めしたHost環境

Core 3.3.12の`esp_idf_version.h`は5.5.5。`usb_host.h`にsuspend/resumeの公開APIはない。
[同tagのHCD](https://github.com/espressif/esp-idf/blob/v5.5.5/components/usb/hcd_dwc.c)の
`_intr_hdlr_hprt`にはREMOTE_WAKEUPのcaseがなく、未知のeventはdefaultでabortする。
[HAL](https://github.com/espressif/esp-idf/blob/v5.5.5/components/hal/usb_dwc_hal.c)にはwake eventのdecodeがある。
そのため、このHostでwake IRQを有効にするだけで自動復帰を期待する手順は採らなかった。
これはsourceを読んだ結果で、benchでabortを起こした結果ではない。取得した原本は`/tmp/espusb-v3-remote-wakeup-research/`。

[最新のESP-USB](https://docs.espressif.com/projects/esp-usb/en/latest/esp32s3/usb_host.html)は
Hostのglobal suspend/resumeとremote wakeupを説明しているが、Core 3.3.12の能力とは扱わない。
この確認のためだけにCoreやpluginを更新しない。

試験の方針：既存のsuspend_resumeと同様に、probeの中でroot portを制御する手動Hostを使う。
全transferを回収してからsuspendし、SDKの未知eventへ渡さずにwake statusを観測し、Hostのresumeを続ける。
Hostから時刻でresumeするだけの試験を、Device起因の成功とは数えない。
許可なし/CLEAR後は信号なし、許可後は検出あり、その後のpayload往復、reset後の許可の消去を区別して照合する。

### 当時の実装案

- 能力設定、Host許可の照会、非同期の要求をDeviceに置く。要求の成功はqueueの受付を示す。
- 既存workerのdeferred callを使い、常駐のtimer/taskを作らない。
- coreのHost許可byteを「許可」と「保留」のflagへ整理し、独立した保留領域を増やさずに重複要求を拒否する。
- `configurationEpoch`をcallbackへ渡し、reset/再構成後の古い要求を実行しない。
- DeferredCallは通常のbus世代の判定より先に実行され、overflow回復でもdrainされる。
  そのためcallback自身が、epoch、stopping/recovering/resetting、構成、Hostの許可、suspendedを確かめ直す。
- queueを破棄する前に公開側を締め、worker/PHYの終了順序を保つ。
