# v3 Video: 独自化前の設定とframe寿命


## Video独自化前の設定・寿命の修正

2026-10-27。Videoの稼働中破棄はDeviceを停止し、USB worker/DMAが止まってから状態を解放する。
prepareToStopで新規frameを拒否するが、稼働中フラグは終了処理まで保持して設定変更を防ぐ。
未登録/二重begin/別runtimeの稼働中beginはInvalidStateとし、startErrorで理由を返す。
raw descriptor writerも稼働中の書換えを拒否する。

format/size/fps/最大frameを候補全体で検証し、失敗時に旧値を保持する。
未知format、YUY2の奇数幅・実サイズと異なる固定frame、32 bit bit rateのoverflowを拒否する。
MJPEGの奇数サイズと1..255 fpsを受け付け、transportと関係しない120 fps制限を撤去した。
開始時にも構成を再検証する。最大4096の寸法制約とVideoの一形式という構成は現状維持。

`video-lifecycle-related.xml`は**2 passed、0 skipped、0 unfinished、344.96秒**。
metadataの反復とheap回復、257/511/512/1021 byteの実ISO frame一致と完了callback、
未登録Videoの生成/破棄後の正常通信、64 KiB送信中のFunction破棄とbuffer解放、既存descriptor試験を含む。
Hostの公開FIFO設定はRX160/NPTX16/PTX16 lines。両case両board STOPPED、panicなし。
serial原本は`/tmp/pytest-embedded/2026-10-27_01-30-16-696638/`、コピーとJUnit/transcriptは`validation/.pytest-results/video-lifecycle-related*`。
通常収集は20件。全体の直近成功はNCM時点の19件で、今回の関連2件と区別する。

Videoのclass/FIFO/global ownerはまだTinyUSBのまま。payload容量/ISO・BulkのAPI化と独自driver、
Probe/Commit検証、reset/alt/end時のframe所有権通知、callback contextの統一は次の実装対象。
今回の実転送はsynthetic MJPEG payloadのUSB転送検証であり、JPEG decode・PC/OS・他SoC/HSの検証ではない。
