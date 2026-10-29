# Typed HIDの送信

Gamepad、Consumer Control、System Controlを各単体と3種を統合したHIDで開始する。
各構成を2回開始し、Hostのraw inputとGET_REPORTを既知のbyte列に照合する。
Gamepadは符号付き6軸、hat、32 bit button、構造体/引数形式とreleaseAllを確認する。
Consumer/Systemはpress/sendUsage/release/clickとusage()を確認し、clickは押下と解放の2 reportを要求する。
HostはS3上のEspUsbHostであり、PC OSのシステム操作・ゲーム互換性試験ではない。

2026-10-29: S3 Host/Deviceのcompileと標準comboによる1件収集は成功。
実機はarchive変更後の全44件がS3ペアを解放してから実行する。現時点では実機成功の記録ではない。
44回のinput/GET_REPORT照合、clickの押下・解放を含む52 input reportを予定する。

初回raw-onlyは1 passed、44.47秒、44照合/52 input、2 logs STOPPEDだった。
ただしSystem Controlのdescriptor意味を検証していないため、正しいusageの証明とは扱わない。
raw-only結果を別名保存して、公開定数とHostのdescriptor解釈を使う試験へ補強する。

System補強後は1 passed、75.44秒、44 input照合/52 input、48 snapshot、4 descriptor写像が成功。
HostがEP0から取得したarray定義を解析し、selector1/2/3→usage129/130/131と0のNull解放を照合した。
公開APIの数値は標準usageへ揃え、定数名を使う呼出しは維持する。
7不正値/API呼出し×4を拒否し、usage()・GET_REPORTを保持、Hostの50 ms監視中も余分なinputなし。
2 logs STOPPED/panicなし。初回のdescriptor不一致の失敗・停止ログもbeforeとして保存した。
記録はvalidation/.pytest-results/hid-system-descriptor-{before,after}*。
S2/P4 profileはDeviceのcompile確認用で、常設s3 comboの実機対象は変えない。
