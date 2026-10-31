# リリース手順

保守者向けの文書です。EspUsbDevice v3 をリリースするときの確認項目と手順を書きます。

- 試験の仕組みは [試験](testing.ja.md)、設計は [設計と理由](architecture.ja.md) を見てください。
- `.github/workflows/release.yml` と `tools/bump_version.py` は共通の release toolkit（[arduino-library-release-toolkit](https://github.com/tanakamasayuki/arduino-library-release-toolkit)）で保守します。このリポジトリでは編集しません。

## 全体の流れ

1. 対象の commit を決め、source を固定する。
2. [文書とメタデータ](#文書とメタデータ) を確かめる。
3. [offline の確認](#offline-の確認) を通す。
4. [examples の build](#examples-の-build) を全 profile で通す。
5. [実機試験](#実機試験) を全 combo で通す。
6. [版番号](#版番号) を確かめ、GitHub Actions の Release workflow を実行する。
7. [リリース後の確認](#リリース後の確認) をする。

途中で source を直したら、その commit で 3〜5 をやり直します。別の commit の結果を流用しません。

## 文書とメタデータ

| 対象 | 確かめること |
| --- | --- |
| `README.md` / `README.ja.md` | 対応 SoC、確認済みの範囲、未確認の範囲（S2 実機、S31、Windows / macOS など）が [試験](testing.ja.md#確認済みの範囲) と一致する |
| `docs/` の利用者向け文書 | API 名、既定値、戻り値が `src/EspUsbDevice.h` と一致する。英語版（`.md`）と日本語版（`.ja.md`）が同じ内容 |
| `examples/README.md` と各 example の README | 実装済みの API で書かれている。必要な board と結線が書いてある |
| `docs/migrating-from-v2.ja.md` / `.md` | v2 からの変更点が漏れていない |
| `TODO.ja.md` | 残作業だけが並んでいる。リリースで未対応のまま出す項目が README の未確認範囲と合っている |
| `CHANGELOG.md` | `## Unreleased` の中身が、利用者向けのリリースノートとして読める。版の見出しは bump script が付けるので書かない |
| `library.properties` | `name`、`sentence`、`paragraph`、`architectures`、`includes` が公開内容と合っている。`paragraph` の class 一覧に抜けがない（DFU など） |
| `keywords.txt` | 主な class、method、定数が入っている |

## offline の確認

repo の root で実行します。board は不要です。

```sh
python3 tools/verify_usb_backend.py
python3 tools/check_doc_links.py
git diff --check
```

| コマンド | 確かめること |
| --- | --- |
| `tools/verify_usb_backend.py` | 保守対象の DWC2 file があり、旧 TinyUSB vendor の path・設定 header・`tusb*.h` の include がない |
| `tools/check_doc_links.py` | Markdown の相対リンクと `#anchor` がすべて解決する。見出しを変えるとリンクは黙って壊れるので、文書を直したら必ず実行する |
| `git diff --check` | 空白の誤り |

CI の `v3-checks.yml` も `verify_usb_backend.py` と `pytest --combo all --collect-only` を実行します。対象 commit で緑であることを確かめます。

## examples の build

全 example を、各 example が宣言した profile で build します。

```sh
python3 tools/build_check.py esp32s3
python3 tools/build_check.py esp32s2
python3 tools/build_check.py esp32p4
python3 tools/build_check.py esp32p4x
python3 tools/build_check.py --list   # どの example がどの profile を持つか
```

- 4 つの profile すべてで `failed=0` を確かめます。
- その profile を宣言していない example は skip され、失敗にはなりません。宣言の漏れがないかを `--list` で確かめます。
- `KILLED by signal` は環境の問題です。build の失敗と区別し、やり直します。
- CI の Build Check（`.github/workflows/build-check.yml`）は `esp32s3`、`esp32p4`、`esp32s2` だけを build します。`esp32p4x` は手元で実行します。
- 出力は `build/examples/<profile>/` に置かれます（git 対象外）。

build probe で、未使用の機能がリンクされないことも確かめます（[試験の build probe](testing.ja.md#build-probe)）。

## 実機試験

`validation/` で、登録した全 combo を対象の commit で実行します。

```sh
cd validation
uv sync --locked
uv run --locked --env-file .env pytest --combo all --collect-only -q
uv run --locked --env-file .env pytest --combo all --maxfail=1000 -q -rs \
  --junitxml=.pytest-results/release.xml
```

確かめること：

- 収集エラーが 0。
- 6 つの combo（`s3`、`p4`、`p4_psram`、`p4_p4x_hs`、`p4x_p4_hs`、`p4x_pc`）がすべて実行された。
- JUnit で failed / error が 0。skipped があれば、理由が port 未設定などの環境で、試験の不備でないこと。
- 各 serial log が正常に停止し、panic がない。
- 実行しなかった combo、skip した test は、リリースノートか README の未確認範囲に書く。合格扱いにしない。

一度に流せない場合は combo ごとに実行してよいですが、すべて同じ commit で行います。

```sh
uv run --locked --env-file .env pytest --combo s3 --maxfail=1000 -q -rs
```

### 実機で確かめられない範囲

次の範囲は bench にないので、リリースの時点の状態を README に書きます。

- ESP32-S2 の実機（compile だけの場合はそう書く）
- ESP32-S31（未対応）
- Windows / macOS
- SD card と OTA の実媒体
- Arduino-ESP32 4 系

## 版番号

### 仕組み

Release workflow（`workflow_dispatch`）は次のことをします。

1. `tools/bump_version.py --level <major|minor|patch>` で `library.properties` の `version` を上げる。
2. `src/espusbdevice_version.h` を作り直し、`CHANGELOG.md` の `## Unreleased` の中身を新しい版の見出しへ移す。
3. その commit を default branch（`main`）へ push する。
4. `release` branch を作り、examples の `sketch.yaml` の `dir:` 参照を `EspUsbDevice (<版>)` に書き換えて commit する。
5. tag `v<版>` を付け、zip を作って GitHub release を作る。

`library.properties`、`src/espusbdevice_version.h`、`CHANGELOG.md` の版の見出しは手で編集しません。

### v3 で確かめること

- **版番号**：workflow は `library.properties` の現在の値を基準に上げます。v3 の branch は v2 の最終版 `2.6.1` のままにしてあり、
  v3 の最初のリリースは `version_level=major` で実行して 3.0.0 にします。版番号を手で 3.x に変えないでください（4.0.0 になってしまいます）。
- **default branch**：workflow は default branch へ push します。v3 を `main` へ取り込んでから実行します。
- **release に含める file**：共通の workflow が `tests/` を除き、`tools/release_hooks/pre_release_commit.py` が `validation/` を除きます。
- **examples の library 参照**：`release` branch では examples の `dir:` 参照が版指定に変わります。default branch では `dir:` のままです。
- **EspUsbHost の版**：examples と validation が指定する EspUsbHost の版（例：2.10.1）が公開済みであること。

### 実行

GitHub の Actions → Release → Run workflow で、`version_level` を選んで実行します。

## リリース後の確認

- tag `v<版>` と GitHub release ができ、zip が添付されている。
- release note に `CHANGELOG.md` の該当の節が入っている。
- `library.properties`、`src/espusbdevice_version.h`、`CHANGELOG.md` の版がそろっている。
- `release` branch の examples が版指定の参照になっている。
- 最終の差分に、build の出力、cache、手元の profile だけの変更が入っていない。
- Arduino Library Manager に新しい版が出たら、別の環境で install して代表の example を build する。
