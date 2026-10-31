# MSC helper の RAM 試験

公開の `EspUsbDeviceMscRamDisk` と `EspUsbDeviceMscFatRamDisk` を MSC に attach し、実際の BOT で読み書きできることを確かめます。
S3 の FS（combo `s3`）と、P4 / P4X の HS 直結（`p4_p4x_hs`、`p4x_p4_hs`）で動きます。

## 目的

- RAM disk：Host が書いた内容を、Host と Device の helper の両方から読み戻して一致すること。
- FAT RAM disk：Host が FAT12 の構造を読み、helper が追加した file を取り出せること。Host が書き換えた内容を Device 側の API で読めること。
- staging の容量を変えても動くこと。

## 手順

- staging を 64 / 192 / 512 byte に変えて BOT を通します（HS では MPS 512 の倍数になるよう 8 倍にします）。
- RAM disk：511 byte sector × 7 を Host が書いて読み戻し、Device の helper からも照合します。
- FAT RAM disk：Host が BPB、2 つの FAT12、directory、cluster chain を読み、helper が追加した 1537 byte の file を全比較します。
  Host が 4 cluster 分を書き換え、Device の `fileSize` / `readFile` で照合します。別の README file が残ることも確かめます。
- 各構成を 2 回開始し、CPU 用の非整列の storage の両端の guard と、RAM の未使用の末尾も確かめます。

## 範囲外

- SD card と OTA の実媒体
- PC の OS での mount
