# リリースチェックリスト

`EspUsbDevice` のリリース前に確認する項目です。GitHub Actions と `tools/` の bump script は
全プロジェクト共通の運用に従い、このリポジトリ固有の手順では編集しません。

## 事前確認

- `README.ja.md` / `README.md` のリリース範囲が実装と合っている。
- `examples/README.ja.md` / `examples/README.md` と各 example README が実装済み API と合っている。
- `docs/DEVELOPMENT_PLAN.ja.md` は時系列ログではなく、現在の方針と残作業を示している。
- `docs/EXAMPLES_COVERAGE.ja.md` は Arduino-ESP32 bundled examples との差分を説明している。
- `tests/TEST_PLAN.ja.md` / `tests/TEST_PLAN.md` は default profile が released `EspUsbHost` を使う方針になっている。
- `TODO.ja.md` は次フェーズ候補を示し、完了済み MVP 項目を未完了として残していない。

## メタデータ

- `library.properties` の `name`、`sentence`、`paragraph`、`architectures`、`includes` が公開内容と合っている。
- `keywords.txt` に主要 class、method、constant が入っている。
- `CHANGELOG.md` と `src/espusbdevice_version.h` は手編集せず、共通 bump script に任せる。

## テスト

ライブラリ更新や profile 切り替え後は stale build cache を避けるため `--clean` を付けます。

```sh
cd tests
uv run --env-file .env pytest --clean
```

必要に応じて個別に確認します。

```sh
uv run --env-file .env pytest examples_compile/ --clean -vv
uv run --env-file .env pytest peer/ --profile=s3_peer_host --clean -vv
uv run --env-file .env pytest loopback/ --profile=p4_loopback --clean -vv
```

## 手動確認

初回リリースでは必須にしませんが、必要に応じて `tests/manual/README.ja.md` の手順で確認します。

- `examples/MSCFatRamDisk`: PC mount、file copy、OS eject、Device 側 file read。
- `examples/MSCSdCard`: SD card mount、Host OS read/write、OS eject。
- `examples/USBVendor`: browser / libusb / WinUSB からの claim、bulk echo、control request、WebUSB URL。

## リリース作業

- bump script で version と changelog を更新する。
- 更新後に `library.properties`、`src/espusbdevice_version.h`、`CHANGELOG.md` の整合性を確認する。
- 最終 diff に意図しない build artifact、cache、local profile 固有の変更が入っていないことを確認する。
- tag / GitHub release を作成する。
