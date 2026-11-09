# MSC helpers in RAM

> 日本語: [README.ja.md](README.ja.md)

Attaches the public `EspUsbDeviceMscRamDisk` and `EspUsbDeviceMscFatRamDisk` to MSC and checks reads and writes through real BOT.
Runs at FS on S3 / S2 (combos `s3`, `s3_s2`, `s2_s3`) and on the P4 / P4X HS link (`p4_p4x_hs`, `p4x_p4_hs`).

## Purpose

- RAM disk: what the Host wrote reads back the same from both the Host and the Device-side helper.
- FAT RAM disk: the Host reads the FAT12 structure and can extract the file the helper added; the Device-side API reads what the Host rewrote.
- It works with different staging capacities.

## Steps

- BOT runs with a staging of 64 / 192 / 512 bytes (eight times that at HS, to stay a multiple of the 512 MPS).
- RAM disk: the Host writes seven 511-byte sectors and reads them back, and the Device helper matches them too.
- FAT RAM disk: the Host reads the BPB, both FAT12 copies, the directory and the cluster chain, and compares the whole 1537-byte file the helper added.
  The Host rewrites four clusters' worth, matched with the Device's `fileSize` / `readFile`. A separate README file must remain.
- Each configuration starts twice; guards at both ends of the unaligned CPU storage and the unused tail of RAM are checked as well.

## Out of scope

- Real SD cards and OTA
- Mounting in a PC operating system
