# 任意MSC helperのRAM試験

公開RamDisk/FatRamDiskをMSCへattachし、64/192/512 byte stagingで実BOTを通す。
RAMは511 byte sector×7のHost書込み・読戻しをDevice helperからも照合する。
FATはHostがBPB・2個のFAT12・directory・cluster chainを読み、helperが追加した1537 byte fileを全比較する。
Hostから4 clusterの内容を書き換え、DeviceのfileSize/readFileで照合し、別のREADME fileの保持も確認する。
各構成を2回開始し、CPU用の非整列storageの両端guardとRAMの未使用末尾も確認する。
このprobeはRAMのみを使う。SD/OTAの実媒体やPC OS mountは対象外。実機結果は未記録。
2026-10-29: S3 Host/Device compileと標準comboの1件収集は成功した。
Host初回のstd::min型不一致を修正し、失敗ログも`.pytest-results/msc-helpers-initial-host-type-error.txt`に保存した。
