# P4 の loopback 試験

一台の P4（旧 P4、revision v1.x）の native HS 口と FS 口をつなぎ、片方を Host、もう片方を Device にして試験します。
Host 側は公開版の EspUsbHost を使います。

| Device 側 | Host 側 | 実際の link |
| --- | --- | --- |
| HS controller（UTMI） | FS controller | FS、12 Mbps、bulk MPS 64 |
| FS controller | HS controller（UTMI） | FS、12 Mbps、bulk MPS 64 |

- 両方の controller の Device 動作を確かめます。link はどちらの向きでも FS です。
- **480 Mbps の HS link はここでは試験しません。** HS は HS 直結の combo（`p4_p4x_hs`、`p4x_p4_hs`、`p4x_pc`）で確かめます。
- HS 対応の controller を Device にすると、link が FS でも bulk の容量は 512 基準で検査されます。たとえば MSC の staging 64 は `begin()` で拒否されます（[HS で変わる値](../../docs/development/testing.ja.md#hs-で変わる値)）。
- 書込み・log 用の port は、試験用の USB 結線とは別です。

## 使い方

`.env` に `TEST_SERIAL_PORT_P4_LOOPBACK` と `TEST_SERIAL_PORT_P4_PSRAM` を設定します。どちらも同じ board の port です。

```sh
cd validation
uv run --locked --env-file .env pytest p4 --combo p4 -q -rs          # PSRAM なしの全 probe
uv run --locked --env-file .env pytest p4/psram --combo p4_psram -q -rs
uv run --locked --env-file .env pytest p4/audio --combo p4 -q -rs    # 一つの probe
```

- `p4` と `p4_psram` は同じ board を使うので、続けて一つずつ実行します。
- `p4_psram` の profile は FQBN に `PSRAM=enabled` を足しています。
- 一部の probe は P4X 用の compile 確認 profile（`p4x_compile`、`p4x_audio_compile`、`p4x_psram_compile`）を持ちます。combo には入っていないので、pytest では実行されません。

## 各 probe の目的

各 test は、両方の controller について Device を何回か開始・停止し、最後に停止したことも確かめます。

| probe | 目的 |
| --- | --- |
| `loopback` | Vendor の copy 往復、借用送信の反復、借用受信 → 借用送信（1〜65537 byte）。Vendor + CDC + Keyboard の複合 |
| `classes` | RAM の MSC（sector 511 / 512）の書込みと読出し、HS controller での staging 64 の拒否と 512 での回復。2 cable の MIDI echo |
| `audio` | UAC1 / UAC2 の ISO で、再生と録音を同時に流して照合する。UAC2 は feedback の更新も確かめる |
| `video` | Bulk と ISO で frame を送り、内容、完了の status と実長、同じ buffer の返却を確かめる |
| `ccid` | 模擬カードの ATR、APDU、Escape。removable とカード固定の両方 |
| `dfu` | RAM backend の download、manifest、abort、runtime detach。GETSTATE / GETSTATUS と poll 時間 |
| `ncm` | NTB 容量を変えて、生の Ethernet frame を往復する。IP / netif は使わない |
| `psram` | MSC の保存先と Vendor の RX / TX ring を PSRAM に置く。ring の折り返しと容量外の guard を確かめる |

## 確かめないこと

- 480 Mbps の HS link
- P4X（HS 直結の combo で確かめます）
- PC の OS の class driver
- 実際の OTA、SD card、codec
- USB の DMA が PSRAM を直接読み書きする構成（製品は内部 RAM の staging を使い、借用転送の PSRAM buffer は拒否します）

## 試験の作り方の注意

- 借用受信は、実際に受信した長さではなく、投稿した容量全体を DMA に貸します。guard は投稿容量の外側に置きます。
- 借用 buffer は 64 byte 境界の内部 RAM にします。
- Host 側の Vendor IN は、公開 EspUsbHost の cache 準備済みの read queue と `onVendorData` callback で受けます。
  同期の `vendorReadSync()` は P4 で受信の最後の byte が化けることがあり、Host 側の未解決事項です（[TODO](../../TODO.ja.md)）。
  `loopback` の `start ... sync` 命令は、この同期経路を比べるための診断用で、通常の pytest では使いません。
- DFU の raw SDK Host 転送では、呼出し側で cache の準備をします。
- NCM は、`networkClose()` → Device の終了 → Host の終了の順にすると次の Host 開始が失敗します（Host 側の未解決事項）。試験は通常の Device 切断 → Host 終了の順にしています。
