# Typed HIDの送信

Gamepad、Consumer Control、System Controlを各単体と3種を統合したHIDで開始する。
各構成を2回開始し、Hostのraw inputとGET_REPORTを既知のbyte列に照合する。
Gamepadは符号付き6軸、hat、32 bit button、構造体/引数形式とreleaseAllを確認する。
Consumer/Systemはpress/sendUsage/release/clickとusage()を確認し、clickは押下と解放の2 reportを要求する。
HostはS3上のEspUsbHostであり、PC OSのシステム操作・ゲーム互換性試験ではない。

2026-10-29: S3 Host/Deviceのcompileと標準comboによる1件収集は成功。
実機はarchive変更後の全44件がS3ペアを解放してから実行する。現時点では実機成功の記録ではない。
44回のinput/GET_REPORT照合、clickの押下・解放を含む52 input reportを予定する。
