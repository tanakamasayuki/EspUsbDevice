# EspUsbDevice v2 Audio Source Provenance

## 目的

v2 Audioを、v1で使用していたEspressif USBAudioCard由来実装の継続改変にしないため、
参照可能な資料と移植禁止対象を記録する。

## v1で削除する由来source

- `src/EspUsbDeviceAudio.cpp`
- `src/EspUsbDeviceAudioDescriptors.h`

両ファイルはGit commit `8c14580`およびv1 maintenance historyから確認できる。v2 branchでは
baseline取得後に削除し、新実装のtemplateやstubには使用しない。

## v2実装で使用できる入力

- USB Implementers Forumが公開するUSB 2.0およびUSB Audio Class仕様
- descriptor/requestのwire format、定数、fieldの意味
- TinyUSBの公開API/header
- third-party dependencyとして同梱するTinyUSB source
- 本プロジェクトのv2 architecture、function/stream model、unit test
- 実機から取得したdescriptor/transfer結果

仕様やprotocolで一意に決まる数値・field順序は実装に必要な事実として扱う。ただし既存の
macro構造、命名、分岐、コメントを再利用しない。

## v2へ移植しないもの

- `TUD_AUDIO10_*` / `TUD_AUDIO20_*`を組み合わせた旧descriptor template
- `tusb_audio_load_descriptor()`の分岐構造
- 旧Audio control callbackの実装
- `_sample_rate`、`_spk_channels`等のglobal state
- `_spk_buf`と`audioReceiveTask`
- Audio専用`esp_event_loop`
- `applyVolume()`の実装
- 旧constructorを中心とするspeaker/microphone card model

## 実装時の確認

- 新descriptor writerを小さいbyte writerから構築する。
- entity graphからdescriptorを生成し、topology別の巨大macroを作らない。
- class requestをentity/control tableからdispatchする。
- playback/capture data planeを別型にする。
- 旧sourceとのdiffではなくUSB仕様に対するbyte-level testで検証する。
- first-party Audio sourceにEspressif由来noticeが残っていないことを検査する。
- third-party sourceのlicense/copyright noticeは変更しない。

## 注意

この記録はlicense noticeを機械的に削除する根拠ではない。由来コードを再利用する場合は
そのlicense条件とnoticeを保持する。v2では由来コードを削除し、独立した設計・実装へ
置き換えることでfirst-party Audio sourceの境界を作る。
