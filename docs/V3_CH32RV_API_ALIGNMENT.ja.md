# CH32RVとの利用者API調整メモ

2026-10-28、ローカル`ArduinoCore-CH32RV/libraries/CH32RVUsbDevice/src/`の公開headerを比較。
当該libraryの直近commitは`d99f8979`。これは移植計画でも、CH32RV側を変更した記録でもない。
ESP側のv3を実測で固め、共通にできる操作・契約をCH32RV側へ共有するためのメモである。
他機種の制約を共通APIの定数へ持ち込まず、名前だけで互換と判断しない。

| 項目 | 現状の差 | v3の判断・調整案 |
| --- | --- | --- |
| DeviceとFunction | 両方ともDeviceにFunctionを関連付ける。CH32RVには既定global Deviceもある | 明示的な関連付けを維持。globalや自動起動をESP側の必須経路にしない |
| begin再呼出し | CH32RVは成功後の再beginを成功扱い。ESPはInvalidStateで拒否 | ESPの拒否を維持。異なるconfigが適用されたと誤解させず、end→configure→beginを共通の再設定手順にする |
| endと所有権 | CH32RVはvoid、ESPはesp_err_t。ESP workerからのendは拒否される | 戻り値より「成功した停止後だけbuffer/Functionを破棄できる」を揃える。ESPの失敗をvoidで隠す互換化はしない |
| started/ready | CH32RVにもstarted/readyがあり、readyはmountedの別名 | ESPもstartedは資源所有、readyはHost構成済みとした。reset時にready=falseでもstartedはtrue |
| 容量設定 | CH32RV CDCはsetBuffers、Vendor容量はstatic。ESPは共通bulk configureとinstance実効容量 | 共有候補は要求容量・任意外部領域・実効容量の区別。既定容量やDMA alignmentまで同じ数値にしない |
| copy write | CH32RV CDCはHostが開いていれば満杯待ち、閉じていればdrop。ESPは受理数を返すpartial/nonblocking | ESPのpartialを維持。移行側が残りを保持し、速度差で無言の欠落やUSB workerのblockを作らない |
| CDCの型 | CH32RVはArduino Stream派生、ESPは共通bulkのPrint派生でread/availableを提供 | 基本のread/writeは近いが、Stream参照・peek・readBytes等まで互換とはしない。Stream化は追加stateと待ちの意味を確認する次のAPI検討候補 |
| 空き容量と待ち | CH32RV CDCはavailableForWrite、VendorはwriteAvailable。waitWritableは容量超過をclamp | ESP bulk共通のwriteAvailableを維持。waitWritableの要求超過は拒否する。名前の統一だけで待ち・clamp・dropの差を隠さない |
| 通知context | CH32RVは主にdeliverEvents、ESPの転送完了はUSB worker | 共通名でも同じ実行contextではない。block/end禁止と次投稿可否を契約へ明記する。cooperative task APIをESPへno-opで残さない |
| borrowed TX | CH32RVはbool＋lastDirectError、onTxCompleteへ完了を通知。ESPは投稿結果/idと型付き返却 | ESPの所有権返却を維持。reset/stop/cancel/errorでも一度返す契約を共有候補にする。alignment違いのために隠れcopyを入れない |
| control callback | CH32RVの公開requestにはactualBytesがない。ESPはstageと実data量を公開 | stageごとの意味と実量を合わせる候補。rawRequestの内部pointerを保存して遅延返信できると約束しない |

比較した主なheaderは`CH32RVUsbDevice.h`、`CH32RVUsbDeviceVendor.h`、
`CH32RVUsbDeviceCdcSerial.h`。全クラス・全戻り値の互換表ではない。
ESP固有のworker/core/cache、CH32RV固有のclock/USBSS/cooperative実行は各platformへ残す。

基本例の宣言、configure、begin、read/writeという操作感を優先する。
CH32RV側の更新案を作る際は、容量切替・partial write・reset中の借用返却の例を両方で比較し、
利点と既存利用への影響を確認する。CH32RV実装やOEPへの追従をESP側の合否基準にはしない。
