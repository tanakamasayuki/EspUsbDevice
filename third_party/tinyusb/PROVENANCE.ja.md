# USB backendの由来と保守

[English](PROVENANCE.md)

v3のbuildからTinyUSB vendor snapshot、設定header、class driver、OS/FIFO基盤と
自動updaterを撤去した。core/EP0/class dispatch/runtime資源管理は本ライブラリが持つ。
このディレクトリは由来とlicenseの記録であり、更新するbuild依存pinではない。

DWC2 hardware実装は由来コードを保守するforkで、clean-roomでの再実装ではない。
`ORIGIN.json`は元のfull SHAとvendor選択が残っていた最後のlocal commitを記録する。
派生source内のMIT著作権表示と`LICENSE`、ST register定義のBSD-3-Clause条件
`LICENSE-ST`を保持する。

| 保守するsource | 由来と役割 |
| --- | --- |
| `src/internal/EspUsbDwc2Device.c` | DWC2 Device DMA/endpoint処理。runtime資源、lock、取消し、interval検証を本体で保守 |
| `src/internal/EspUsbDwc2Phy.c` | reset、PHY選択、turnaround |
| `src/internal/EspUsbDwc2Hardware.h` | registerアクセスとFIFO flush |
| `src/internal/EspUsbDwc2Registers.h` | Device専用register map。未使用Host/channel/FIFO windowを除去 |
| `src/internal/EspUsbDwc2Platform.*` | 旧MIT ESP port由来のmetadata、IRQ/cache/pull処理 |

controller event、CDC/MIDI descriptor layout、US keyboard lookup行の由来表示も保持する。
削除したsource、patch manifest、自動updaterはGit履歴で参照できる。

`python3 tools/verify_usb_backend.py`で保守対象fileと旧vendor build/configuration経路の
不在をoffline確認する。この検査だけでUSBの正しさや全間接依存の不在は証明できない。
compiler依存、link symbolの実装位置、`validation/`の標準`--combo all`も確認する。
upstream修正は内容をreviewして保守実装へ反映し、snapshotの自動置換は行わない。

転送recordは引き続きdescriptorで選択した方向だけ確保する。専用mapはoffset/strideを
static assertし、S3 probeではSDK register構造とも主要位置を比較する。
不正な周期intervalをFIFO確保前に拒否し、32768までのinterval/retryを表現する。

これはsource/依存の所有を整理する区切りで、v3全体の完了ではない。
全EP0/HALT競合、最終API/sample、OS互換性、S2/P4/HS実機確認は後続。
S31はSDK対応、metadata、実機検証が必要。
