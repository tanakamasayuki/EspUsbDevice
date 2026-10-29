# Remote wakeupのS3–S3実験

標準plugin combo/peersを使い、Hostの実wake statusを検出してからresumeする。
ピン留めしたIDF5.5.5のHCDはremote event未対応のため、probe内で無転送時だけUSB IRQを保留し、
clock gate/ungateを含めてroot portを復帰させ、pending wakeをclear後に元のIRQ policyを戻す。
SDKやpytestへのpatch/conftestは使わない。

最初の基本経路は1 passed、50.20秒、2 logs停止/panicなし。
3起因wake、6無信号、11状態拒否、10payload往復、Host許可のCLEAR/resetを確認した。
初回のinclude不足と、clock gate不足のHost制御の失敗も別名で保存している。
原本/tmp/pytest-embedded/2026-10-29_09-45-28-872187/。
Device側register観測とevent hookは診断用で、公開APIには加えない。
[API・費用・規格](../../../docs/V3_REMOTE_WAKEUP.ja.md)を参照。

保留reset/重複要求とend並行呼出しを追加。停止guard後は1 passed、45.24秒、2 logs停止/panicなし。
workerを保留したままruntimeの停止を開始し、別taskの17照会/要求を拒否してからworkerを再開する。
end復帰後の32照会/要求も拒否し、heap integrityを確認した。
原本/tmp/pytest-embedded/2026-10-29_10-21-10-501656/、remote-wakeup-stop-after*。
停止開始を見ない版の失敗と、基本境界成功51.63秒は別に保存した。
