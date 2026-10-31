# 任意MSC helperのRAM試験

2026-10-31からは、同じprobeをP4/P4XのHS直結（combo `p4_p4x_hs`/`p4x_p4_hs`）でも実行する。
HSでの結果は[P4/P4Xと試験環境](../../../docs/V3_P4_VARIANTS_AND_BENCH.ja.md#s3-peer試験をhs直結へ広げた結果2026-10-31)を参照。以下の記録はS3–S3での結果。

公開RamDisk/FatRamDiskをMSCへattachし、64/192/512 byte stagingで実BOTを通す。
RAMは511 byte sector×7のHost書込み・読戻しをDevice helperからも照合する。
FATはHostがBPB・2個のFAT12・directory・cluster chainを読み、helperが追加した1537 byte fileを全比較する。
Hostから4 clusterの内容を書き換え、DeviceのfileSize/readFileで照合し、別のREADME fileの保持も確認する。
各構成を2回開始し、CPU用の非整列storageの両端guardとRAMの未使用末尾も確認する。
このprobeはRAMのみを使う。SD/OTAの実媒体やPC OS mountは対象外。実機は1 passed、39.64秒。8開始/8 guard検査、raw/FAT各4往復、2 logs STOPPED/panicなし。
原本は/tmp/pytest-embedded/2026-10-29_05-58-02-529930/、記録はvalidation/.pytest-results/msc-helpers*。
2026-10-29: S3 Host/Device compileと標準comboの1件収集は成功した。
Host初回のstd::min型不一致を修正し、失敗ログも`.pytest-results/msc-helpers-initial-host-type-error.txt`に保存した。
